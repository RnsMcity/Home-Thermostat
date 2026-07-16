#include "workspace_states.h"
#include <string.h>
#include <math.h>
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
    snprintf(_dbg_buf, sizeof(_dbg_buf), "[DBG] " fmt "\r\n", ##__VA_ARGS__); \
    dbg_print(_dbg_buf); \
} while(0)

/* ---------------------------------------------------------------------------
 * Module-level static state
 * --------------------------------------------------------------------------- */
static system_state_t    g_current_state = STATE_IDLE;
static telemetry_data_t  g_telemetry     = { 0 };
static notif_queue_t     g_notif_queue;
static uart_parser_t     g_uart_parser;

/* Celebration timer — 3 s per spec §5 ------------------------------------- */
#define CELEBRATE_DURATION_MS   3000U
static uint32_t g_celebrate_start_tick = 0;

/* Demo auto-cycle tracking ------------------------------------------------- */
static uint32_t g_last_cycle_tick = 0;

/* ---------------------------------------------------------------------------
 * GPIO LED init for STM32469I-DISCO onboard LEDs
 * LD1(Green)=PG6, LD2(Orange)=PD4, LD3(Red)=PD5, LD4(Blue)=PK3
 * --------------------------------------------------------------------------- */

static void demo_leds_init(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOK_CLK_ENABLE();

    /* Green LED (PG6) — default ON */
    gpio.Pin   = DEMO_LED_GREEN_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DEMO_LED_GREEN_PORT, &gpio);
    HAL_GPIO_WritePin(DEMO_LED_GREEN_PORT, DEMO_LED_GREEN_PIN, GPIO_PIN_SET);

    /* Orange LED (PD4) — OFF */
    gpio.Pin   = DEMO_LED_ORANGE_PIN;
    HAL_GPIO_Init(DEMO_LED_ORANGE_PORT, &gpio);
    HAL_GPIO_WritePin(DEMO_LED_ORANGE_PORT, DEMO_LED_ORANGE_PIN, GPIO_PIN_RESET);

    /* Red LED (PD5) — OFF */
    gpio.Pin   = DEMO_LED_RED_PIN;
    HAL_GPIO_Init(DEMO_LED_RED_PORT, &gpio);
    HAL_GPIO_WritePin(DEMO_LED_RED_PORT, DEMO_LED_RED_PIN, GPIO_PIN_RESET);

    /* Blue LED (PK3) — OFF */
    gpio.Pin   = DEMO_LED_BLUE_PIN;
    HAL_GPIO_Init(DEMO_LED_BLUE_PORT, &gpio);
    HAL_GPIO_WritePin(DEMO_LED_BLUE_PORT, DEMO_LED_BLUE_PIN, GPIO_PIN_RESET);
}

/* ---------------------------------------------------------------------------
 * State machine core
 * --------------------------------------------------------------------------- */

void state_machine_init(void)
{
    DBG_LOG("System initializing...");
    g_current_state = STATE_IDLE;
    notif_queue_init(&g_notif_queue);
    uart_parser_init(&g_uart_parser);
    memset(&g_telemetry, 0, sizeof(g_telemetry));

    /* Initialize onboard LEDs */
    demo_leds_init();

    /* Set initial LED pattern (IDLE = green on) */
    g_last_cycle_tick = HAL_GetTick();
}

void state_machine_update(uint32_t now_ms)
{
    static uint32_t last_log_tick = 0;

    switch (g_current_state) {

    case STATE_IDLE:
        /* Green ON, others OFF — solid idle indicator */
        HAL_GPIO_WritePin(DEMO_LED_GREEN_PORT, DEMO_LED_GREEN_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(DEMO_LED_ORANGE_PORT, DEMO_LED_ORANGE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DEMO_LED_RED_PORT,    DEMO_LED_RED_PIN,    GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DEMO_LED_BLUE_PORT,   DEMO_LED_BLUE_PIN,   GPIO_PIN_RESET);

        /* Log state transition */
        if (now_ms - last_log_tick >= 100) {
            DBG_LOG("STATE: IDLE | CPU=%u%% GPU=%u%% NET_UP=%ld NET_DOWN=%ld",
                    g_telemetry.cpu_load, g_telemetry.gpu_temp,
                    (long)g_telemetry.net_speed_up, (long)g_telemetry.net_speed_down);
            last_log_tick = now_ms;
        }

        /* Auto-cycle to next state after interval */
        if ((now_ms - g_last_cycle_tick) >= DEMO_CYCLE_INTERVAL_MS) {
            DBG_LOG("Transition → STATE_MEETING");
            g_current_state = STATE_MEETING;
            g_celebrate_start_tick = now_ms;
            g_last_cycle_tick = now_ms;
        }
        break;

    case STATE_MEETING:
        /* Red LED blink at ~2 Hz (250 ms on, 250 ms off) */
        if ((now_ms % 500) < 250) {
            HAL_GPIO_WritePin(DEMO_LED_RED_PORT, DEMO_LED_RED_PIN, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(DEMO_LED_RED_PORT, DEMO_LED_RED_PIN, GPIO_PIN_RESET);
        }
        /* Other LEDs off during meeting */
        HAL_GPIO_WritePin(DEMO_LED_GREEN_PORT, DEMO_LED_GREEN_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DEMO_LED_ORANGE_PORT, DEMO_LED_ORANGE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DEMO_LED_BLUE_PORT,   DEMO_LED_BLUE_PIN,  GPIO_PIN_RESET);

        /* Auto-cycle to next state after interval */
        if ((now_ms - g_last_cycle_tick) >= DEMO_CYCLE_INTERVAL_MS) {
            DBG_LOG("Transition → STATE_CELEBRATE");
            g_current_state = STATE_CELEBRATE;
            g_celebrate_start_tick = now_ms;
            g_last_cycle_tick = now_ms;
        }
        break;

    case STATE_CELEBRATE:
        if ((now_ms - g_celebrate_start_tick) >= CELEBRATE_DURATION_MS) {
            DBG_LOG("Transition → STATE_IDLE (celebration ended)");
            /* Timer expired → restore idle */
            g_current_state = STATE_IDLE;
            g_last_cycle_tick = now_ms;
        } else {
            /* Rapid chase: all 4 LEDs blink in sequence at ~10 Hz */
            uint32_t phase = (now_ms / 100) % 4;
            HAL_GPIO_WritePin(DEMO_LED_GREEN_PORT, DEMO_LED_GREEN_PIN,
                              (phase == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(DEMO_LED_ORANGE_PORT, DEMO_LED_ORANGE_PIN,
                              (phase == 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(DEMO_LED_RED_PORT,    DEMO_LED_RED_PIN,
                              (phase == 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(DEMO_LED_BLUE_PORT,   DEMO_LED_BLUE_PIN,
                              (phase == 3) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
        break;
    }

    /* Drain notification queue → push to LVGL UI */
    char notif_buf[NOTIF_PAYLOAD_MAX];
    while (notif_queue_pop(&g_notif_queue, notif_buf, sizeof(notif_buf))) {
        trigger_lvgl_ui_refresh();
    }
}

/* ---------------------------------------------------------------------------
 * Telemetry handlers  (§4.2)
 * --------------------------------------------------------------------------- */

void handle_cpu_metric(uint8_t load, uint8_t temp)
{
    g_telemetry.cpu_load = load;
    g_telemetry.cpu_temp = temp;
}

void handle_gpu_metric(uint8_t load, uint8_t temp)
{
    g_telemetry.gpu_load = load;
    g_telemetry.gpu_temp = temp;
}

void handle_net_metric(int32_t speed_up, int32_t speed_down)
{
    g_telemetry.net_speed_up   = speed_up;
    g_telemetry.net_speed_down = speed_down;
}

/* ---------------------------------------------------------------------------
 * Teams / GitLab handlers  (§4.3)
 * --------------------------------------------------------------------------- */

void handle_teams_status(teams_status_t status)
{
    switch (status) {
    case TEAMS_AVAILABLE:
        if (g_current_state == STATE_MEETING) {
            g_current_state = STATE_IDLE;
            g_last_cycle_tick = HAL_GetTick();
        }
        break;

    case TEAMS_BUSY:
        if (g_current_state != STATE_MEETING) {
            g_current_state = STATE_MEETING;
            g_celebrate_start_tick = HAL_GetTick();
            g_last_cycle_tick = HAL_GetTick();
        }
        break;

    case TEAMS_AWAY:
        /* Dim backlight / shift to idle — already idle by default */
        break;
    }
}

void handle_gitlab_mr(const char *branch_name)
{
    /* Enqueue notification for UI display */
    notif_queue_push(&g_notif_queue, branch_name, HAL_GetTick());

    if (g_current_state != STATE_CELEBRATE) {
        g_celebrate_start_tick = HAL_GetTick();
        g_last_cycle_tick = HAL_GetTick();
        g_current_state = STATE_CELEBRATE;
    }
}

/* ---------------------------------------------------------------------------
 * Notification queue  (static ring buffer — no malloc)
 * --------------------------------------------------------------------------- */

void notif_queue_init(notif_queue_t *q)
{
    q->head   = 0;
    q->tail   = 0;
    q->count  = 0;
}

uint8_t notif_queue_push(notif_queue_t *q, const char *text, uint32_t ts)
{
    if (q->count >= NOTIF_QUEUE_MAX) return 0; /* full */

    notification_entry_t *entry = &q->entries[q->head];
    size_t len = strlen(text);
    if (len >= NOTIF_PAYLOAD_MAX) len = NOTIF_PAYLOAD_MAX - 1;
    memcpy(entry->text, text, len);
    entry->text[len] = '\0';
    entry->timestamp = ts;

    q->head = (q->head + 1) % NOTIF_QUEUE_MAX;
    q->count++;
    return 1;
}

uint8_t notif_queue_pop(notif_queue_t *q, char *out_buf, size_t out_size)
{
    if (q->count == 0) return 0; /* empty */

    notification_entry_t *entry = &q->entries[q->tail];
    size_t len = strlen(entry->text);
    if (len >= out_size) len = out_size - 1;
    memcpy(out_buf, entry->text, len);
    out_buf[len] = '\0';

    q->tail = (q->tail + 1) % NOTIF_QUEUE_MAX;
    q->count--;
    return 1;
}

/* ---------------------------------------------------------------------------
 * UART parser  (§4.1 — non-blocking state machine)
 * --------------------------------------------------------------------------- */

void uart_parser_init(uart_parser_t *p)
{
    p->state       = PARSER_WAIT_START;
    p->cmd_id      = 0;
    p->expected_len = 0;
    p->data_idx    = 0;
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

/* Internal dispatch — routes parsed packet to the correct handler */
static void uart_parser_dispatch(uart_parser_t *p)
{
    switch (p->cmd_id) {

    case CMD_CPU_METRIC:
        if (p->data_idx >= 2)
            handle_cpu_metric(p->data_buf[0], p->data_buf[1]);
        break;

    case CMD_GPU_METRIC:
        if (p->data_idx >= 2)
            handle_gpu_metric(p->data_buf[0], p->data_buf[1]);
        break;

    case CMD_NET_METRIC:
        if (p->data_idx >= 4) {
            int32_t up, down;
            memcpy(&up,   &p->data_buf[0], sizeof(int32_t));
            memcpy(&down, &p->data_buf[4], sizeof(int32_t));
            handle_net_metric(up, down);
        }
        break;

    case CMD_TEAMS_STATUS:
        if (p->data_idx >= 1)
            handle_teams_status((teams_status_t)p->data_buf[0]);
        break;

    case CMD_GITLAB_MR:
        if (p->data_idx > 0) {
            p->data_buf[p->data_idx] = '\0';
            handle_gitlab_mr((const char *)p->data_buf);
        }
        break;

    default:
        /* Unknown command — silently discard */
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Mock hardware functions  (allow compilation without display / LED hardware)
 * --------------------------------------------------------------------------- */

void set_shelf_leds_raw(const rgb_color_t *colors, uint16_t count)
{
    /*
     * TODO: Replace with actual WS2812B driver.
     *
     * Recommended approach for STM32F469:
     *   - Use SPI DMA in full-duplex mode (WS2812B is 800 kHz PWM-over-SPI)
     *   - Or use TIM1 CH1 with PWM + DMAMUX double-buffer
     *
     * For now, this stub silently accepts the call so the project links.
     */
    (void)colors;
    (void)count;
}

void trigger_lvgl_ui_refresh(void)
{
    /*
     * TODO: Replace with actual LVGL flush callback invocation.
     *
     * Typical pattern:
     *   lv_disp_flush_ready(disp_drv);
     *
     * Or, if using a dedicated display task:
     *   osKernelSuspend();  // FreeRTOS-style signal
     *   lv_task_handler();
     *   osKernelResume();
     *
     * This stub is a no-op so the project compiles without LVGL init.
     */
}

/* ---------------------------------------------------------------------------
 * Accessors
 * --------------------------------------------------------------------------- */

system_state_t get_current_state(void)
{
    return g_current_state;
}

telemetry_data_t *get_telemetry_ptr(void)
{
    return &g_telemetry;
}

notif_queue_t  *get_notif_queue_ptr(void)
{
    return &g_notif_queue;
}

uart_parser_t  *get_uart_parser_ptr(void)
{
    return &g_uart_parser;
}
