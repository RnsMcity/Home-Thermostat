#include "thermostat_state.h"
#include <string.h>
#include <stdio.h>

/* Forward declaration — defined below, used in feed_byte */
static void uart_parser_dispatch(uart_parser_t *p);

/* UART handle — defined in main.c */
extern UART_HandleTypeDef huart3;

/* ---------------------------------------------------------------------------
 * Debug logging — writes directly via HAL_UART_Transmit on USART3
 * --------------------------------------------------------------------------- */
void dbg_print(const char *msg)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), 10);
}

#define DBG_LOG(fmt, ...) do { \
    char _dbg_buf[128]; \
    snprintf(_dbg_buf, sizeof(_dbg_buf), "[THERMO] " fmt "\r\n", ##__VA_ARGS__); \
    dbg_print(_dbg_buf); \
} while(0)

/* ---------------------------------------------------------------------------
 * Module-level static state (no malloc in embedded loops)
 * Current temperature is kept in tenths of a degree so the demo drift can be
 * smooth; the public API exposes whole degrees.
 * --------------------------------------------------------------------------- */
static thermo_mode_t g_mode            = MODE_HEAT;
static bool          g_fan_on          = false;
static int16_t       g_current_tenths  = 720;   /* 72.0 °F */
static int16_t       g_setpoint_f      = THERMO_SETPOINT_MIN + 24;  /* 74 °F */
static uart_parser_t g_uart_parser;

/* ---------------------------------------------------------------------------
 * Onboard LEDs (debug only) — LD1(Green)=PG6, LD2(Orange)=PD4,
 * LD3(Red)=PD5, LD4(Blue)=PK3. thermo_update() owns these pins every loop.
 * --------------------------------------------------------------------------- */
#define LED_GREEN_PORT  GPIOG
#define LED_GREEN_PIN   GPIO_PIN_6
#define LED_ORANGE_PORT GPIOD
#define LED_ORANGE_PIN  GPIO_PIN_4
#define LED_RED_PORT    GPIOD
#define LED_RED_PIN     GPIO_PIN_5
#define LED_BLUE_PORT   GPIOK
#define LED_BLUE_PIN    GPIO_PIN_3

static void demo_leds_init(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOK_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = LED_GREEN_PIN;  HAL_GPIO_Init(LED_GREEN_PORT, &gpio);
    gpio.Pin = LED_ORANGE_PIN; HAL_GPIO_Init(LED_ORANGE_PORT, &gpio);
    gpio.Pin = LED_RED_PIN;    HAL_GPIO_Init(LED_RED_PORT, &gpio);
    gpio.Pin = LED_BLUE_PIN;   HAL_GPIO_Init(LED_BLUE_PORT, &gpio);

    HAL_GPIO_WritePin(LED_GREEN_PORT,  LED_GREEN_PIN,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_ORANGE_PORT, LED_ORANGE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_PORT,   LED_BLUE_PIN,   GPIO_PIN_RESET);
}

static void mode_leds_write(thermo_mode_t mode, bool fan_on)
{
    /* One solid LED per active mode + orange for the independent fan (debug). */
    uint8_t green  = (mode == MODE_AUTO);
    uint8_t orange = fan_on;
    uint8_t red    = (mode == MODE_HEAT);
    uint8_t blue   = (mode == MODE_AC);

    HAL_GPIO_WritePin(LED_GREEN_PORT,  LED_GREEN_PIN,  green  ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_ORANGE_PORT, LED_ORANGE_PIN, orange ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    red    ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_PORT,   LED_BLUE_PIN,   blue   ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---------------------------------------------------------------------------
 * State model core (Section 4 of SPECIFICATION.md)
 * --------------------------------------------------------------------------- */

void thermo_init(void)
{
    g_mode           = MODE_HEAT;
    g_fan_on         = false;
    g_current_tenths = 720;
    g_setpoint_f     = 74;

    demo_leds_init();
    uart_parser_init(&g_uart_parser);

    DBG_LOG("Thermostat state initialized (mode=HEAT, fan=off, cur=%dF, sp=%dF)",
            thermo_get_current_temp_f(), g_setpoint_f);
}

void thermo_update(uint32_t now_ms)
{
#if THERMO_DEMO_SIM_ENABLED
    static uint32_t last_drift_tick = 0;

    /* Demo simulation: in HEAT/AC the current temp drifts toward the setpoint
     * at ~0.1 °F/s (one tenth per 100 ms) and holds within 0.3 °F of it.
     * AUTO holds steady; fan state never affects temperature. Disabled by
     * default — the thermistor feed in main.c owns g_current_tenths now. */
    if ((g_mode == MODE_HEAT || g_mode == MODE_AC) &&
        (now_ms - last_drift_tick >= 100)) {
        last_drift_tick = now_ms;

        int32_t target   = (int32_t)g_setpoint_f * 10;
        int32_t diff     = target - g_current_tenths;
        if (diff > 3)       g_current_tenths++;
        else if (diff < -3) g_current_tenths--;
    }
#else
    (void)now_ms;
#endif

    mode_leds_write(g_mode, g_fan_on);
}

void thermo_set_mode(thermo_mode_t mode)
{
    if ((uint8_t)mode > (uint8_t)MODE_AUTO) return;
    if (g_mode == mode) return;

    g_mode = mode;
    DBG_LOG("Mode -> %d", (int)mode);
}

void thermo_set_fan(bool on)
{
    if (on == g_fan_on) return;

    g_fan_on = on;
    DBG_LOG("Fan -> %s", on ? "on" : "off");
}

bool thermo_get_fan(void)
{
    return g_fan_on;
}

void thermo_adjust_setpoint(int16_t delta)
{
    int32_t next = (int32_t)g_setpoint_f + delta;
    if (next < THERMO_SETPOINT_MIN) next = THERMO_SETPOINT_MIN;
    if (next > THERMO_SETPOINT_MAX) next = THERMO_SETPOINT_MAX;

    g_setpoint_f = (int16_t)next;
}

void thermo_set_current_temp_f(int16_t temp_f)
{
    g_current_tenths = (int16_t)((int32_t)temp_f * 10);
}

/* Sensor feed — clamped to a plausible range so a disconnected or saturated
 * thermistor can't drive the display to nonsense values. */
void thermo_set_current_temp_f_tenths(int32_t tenths)
{
    if (tenths < -400)  tenths = -400;   /* −40 °F */
    if (tenths > 2120)  tenths = 2120;   /* 212 °F */
    g_current_tenths = (int16_t)tenths;
}

void thermo_set_setpoint_f(int16_t setpoint_f)
{
    if (setpoint_f < THERMO_SETPOINT_MIN) setpoint_f = THERMO_SETPOINT_MIN;
    if (setpoint_f > THERMO_SETPOINT_MAX) setpoint_f = THERMO_SETPOINT_MAX;
    g_setpoint_f = setpoint_f;
}

thermo_mode_t thermo_get_mode(void)
{
    return g_mode;
}

int16_t thermo_get_current_temp_f(void)
{
    int32_t t = g_current_tenths;
    return (int16_t)((t + (t < 0 ? -5 : 5)) / 10);
}

int16_t thermo_get_setpoint_f(void)
{
    return g_setpoint_f;
}

/* ---------------------------------------------------------------------------
 * UART parser (Section 5 of SPECIFICATION.md — non-blocking state machine)
 * --------------------------------------------------------------------------- */

void uart_parser_init(uart_parser_t *p)
{
    p->state        = PARSER_WAIT_START;
    p->cmd_id       = 0;
    p->expected_len = 0;
    p->data_idx     = 0;
}

void uart_parser_feed_byte(uart_parser_t *p, uint8_t byte)
{
    switch (p->state) {

    case PARSER_WAIT_START:
        if (byte == PKT_START) p->state = PARSER_GET_CMD;
        break;

    case PARSER_GET_CMD:
        p->cmd_id = byte;
        p->state  = PARSER_GET_LEN;
        break;

    case PARSER_GET_LEN:
        p->expected_len = byte;
        p->data_idx     = 0;
        if (p->expected_len == 0) {
            /* Zero-length payload → dispatch immediately */
            uart_parser_dispatch(p);
            p->state = PARSER_WAIT_START;
        } else {
            p->state = PARSER_READ_DATA;
        }
        break;

    case PARSER_READ_DATA:
        if (p->data_idx < sizeof(p->data_buf)) {
            p->data_buf[p->data_idx++] = byte;
        }
        if (p->data_idx >= p->expected_len) {
            uart_parser_dispatch(p);
            p->state = PARSER_WAIT_START;
        }
        break;

    case PARSER_WAIT_END:
        /* Should not reach here with current state flow, but guard anyway */
        p->state = PARSER_WAIT_START;
        break;
    }
}

/* Internal dispatch — routes parsed packet to the correct handler.
 * Note: the END byte (0x03) is consumed as the next WAIT_START probe and is
 * never validated (see SPECIFICATION.md §5). */
static void uart_parser_dispatch(uart_parser_t *p)
{
    switch (p->cmd_id) {

    case CMD_SET_CURRENT_TEMP:
        if (p->data_idx >= 2) {
            int16_t tenths;
            memcpy(&tenths, &p->data_buf[0], sizeof(tenths));
            thermo_set_current_temp_f((int16_t)(tenths / 10));
            DBG_LOG("UART: current temp set to %dF", thermo_get_current_temp_f());
        }
        break;

    case CMD_SET_MODE:
        if (p->data_idx >= 1) {
            thermo_set_mode((thermo_mode_t)p->data_buf[0]);
        }
        break;

    case CMD_GET_STATE:
        /* No TX response path yet — see SPECIFICATION.md §5 */
        DBG_LOG("UART: GET_STATE (not implemented)");
        break;

    case CMD_SET_SETPOINT:
        if (p->data_idx >= 2) {
            int16_t tenths;
            memcpy(&tenths, &p->data_buf[0], sizeof(tenths));
            thermo_set_setpoint_f((int16_t)(tenths / 10));
            DBG_LOG("UART: setpoint set to %dF", g_setpoint_f);
        }
        break;

    case CMD_SET_FAN:
        if (p->data_idx >= 1) {
            thermo_set_fan(p->data_buf[0] != 0);
        }
        break;

    default:
        /* Unknown command — silently discard */
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Accessors
 * --------------------------------------------------------------------------- */

uart_parser_t *get_uart_parser_ptr(void)
{
    return &g_uart_parser;
}
