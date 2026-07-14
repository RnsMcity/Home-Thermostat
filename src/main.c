#include "workspace_states.h"

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
void SystemClock_Config(void);

/* ---------------------------------------------------------------------------
 * UART receive buffer — filled by HAL_UART_RxCpltCallback (user-implemented)
 * --------------------------------------------------------------------------- */
static uart_parser_t *g_parser;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* Feed each received byte into the non-blocking parser */
    extern uint8_t g_rx_byte;  /* Set by ISR before calling this callback */
    uart_parser_feed_byte(g_parser, g_rx_byte);
}

/* ---------------------------------------------------------------------------
 * Main — master application entry point
 * --------------------------------------------------------------------------- */

int main(void)
{
    /* HAL init (board-specific: clocks, SDRAM, LTDC, UART, Timers…) */
    HAL_Init();

    /* Board-level peripheral initialisation provided by CubeMX / BSP */
    SystemClock_Config();
    // BSP_LCD_Init();          /* 800×480 RGB LCD via LTDC + SDRAM */
    // BSP_UART_Init();         /* UART @ 115200 baud for host daemon */
    // BSP_LED_Init();          /* WS2812B LED strip (PWM / SPI) */

    /* Application-level initialisation */
    g_parser = get_uart_parser_ptr();
    state_machine_init();

    uint32_t last_tick = HAL_GetTick();

    /* -------------------------------------------------------------------
     * Main event loop — strictly non-blocking, 5 ms pacing via HAL ticks.
     * No HAL_Delay() anywhere.
     * ------------------------------------------------------------------- */
    while (1) {
        uint32_t now = HAL_GetTick();

        /* Pace the loop to ~5 ms intervals */
        if ((now - last_tick) < 5) continue;
        last_tick = now;

        /* Update async state machine with current tick count */
        state_machine_update(now);

        /* LVGL task handler — must be called periodically for UI rendering */
        // lv_task_handler();

        /* Yield to idle (optional: WFI instruction for power saving) */
        __WFI();
    }

    /* Unreachable — but silence compiler warnings */
    return 0;
}

/* ---------------------------------------------------------------------------
 * Stub for SystemClock_Config — replace with CubeMX-generated implementation
 * --------------------------------------------------------------------------- */
void SystemClock_Config(void)
{
    /*
     * TODO: Replace with actual clock configuration from CubeMX.
     *
     * Target: STM32F469NIH6 @ 180 MHz
     *   - HSE = 8 MHz crystal
     *   - PLL → 180 MHz (VCO = 360 MHz, SYSCLK div = 2)
     *   - AHB = 180 MHz, APB1 = 45 MHz, APB2 = 90 MHz
     */
}

/* ---------------------------------------------------------------------------
 * Error handler — halt on unexpected HAL failures
 * --------------------------------------------------------------------------- */
void Error_Handler(void)
{
    /* TODO: Flash error LED pattern or log via UART for debug */
    while (1) {
        __WFI();
    }
}
