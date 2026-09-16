#include <stdio.h>
#include <string.h>

#include "stm32f4xx_hal.h"
#include "thermostat_state.h"
#include "lvgl_display.h"
#include "ui_main_screen.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_sdram.h"
#include "stm32469i_discovery.h"

/* Peripheral Handlers */
UART_HandleTypeDef huart3;

/* Private Function Prototypes */
static void MX_USART3_UART_Init(void);
void SystemClock_Config(void);

/* Debug logging goes through dbg_print() in thermostat_state.c */
#define DBG_LOG(fmt, ...) do { \
    char _dbg_buf[128]; \
    snprintf(_dbg_buf, sizeof(_dbg_buf), "[DBG] " fmt "\r\n", ##__VA_ARGS__); \
    dbg_print(_dbg_buf); \
} while(0)

/* Progress LED toggle using LED_ORANGE (LED2) to avoid pin conflicts */
static void progress_blink(void)
{
    BSP_LED_Toggle(LED_ORANGE);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    lv_tick_inc(1); /* LVGL tick — fed from the 1 ms timer interrupt */
}

int main(void)
{
    volatile uint32_t *fb = (volatile uint32_t *)0xC0000000;

    /* 1. Base Hardware Initialization */
    HAL_Init();
    SystemClock_Config();

    /* 2. Configure safe Status LED (LED_ORANGE is on PG6, safe from bus interfaces) */
    BSP_LED_Init(LED_ORANGE);

    /* 3. Initialize USART3 early so every later boot phase is observable on the VCP.
     * Must run after SystemClock_Config() — HAL_UART_Init computes BRR from PCLK1. */
    MX_USART3_UART_Init();
    progress_blink(); // Checkpoint: UART online

    /* 4. Give external hardware (ST-Link / Terminal) a moment to stabilize */
    HAL_Delay(500);
    progress_blink(); // Checkpoint 1

    /* 5. Thermostat state model + onboard debug LEDs */
    thermo_init();
    progress_blink(); // Checkpoint 2

    /* 6. Initialize SDRAM (Framebuffer memory base at 0xC0000000) */
    if (BSP_SDRAM_Init() != SDRAM_OK) {
        while (1) { 
            BSP_LED_Toggle(LED_ORANGE); 
            HAL_Delay(100); 
        }
    }
    progress_blink(); // Checkpoint 3: SDRAM Configured

    /* Verify basic SDRAM retention */
    fb[0] = 0xDEADBEEF;
    fb[1000] = 0x12345678;
    HAL_Delay(10);
    progress_blink(); // Checkpoint 4: SDRAM Integrity Verified

    /* Route LTDC -> DSI before panel init so LP-command windows exist while the
     * OTM8009A is being configured. HAL_DSI_DeInit/Init do not clear WCR, so this
     * survives BSP_LCD_InitEx's internal re-init. */
    extern DSI_HandleTypeDef hdsi_eval;
    hdsi_eval.Instance->WCR |= DSI_WCR_LTDCEN;

    /* 7. Initialize LCD (DSI + LTDC + DMA2D + OTM8009A display driver) */
    uint8_t lcd_status = BSP_LCD_InitEx(LCD_ORIENTATION_LANDSCAPE);
    progress_blink(); // Checkpoint 5: LCD Screen Driver Ready

    /* 8. Configure LTDC layers pointing to the working SDRAM base address */
    BSP_LCD_LayerDefaultInit(LTDC_ACTIVE_LAYER_BACKGROUND, LCD_FB_START_ADDRESS);
    BSP_LCD_LayerDefaultInit(LTDC_ACTIVE_LAYER_FOREGROUND, LCD_FB_START_ADDRESS);
    BSP_LCD_SetLayerVisible(LTDC_ACTIVE_LAYER_BACKGROUND, ENABLE);
    BSP_LCD_SetLayerVisible(LTDC_ACTIVE_LAYER_FOREGROUND, ENABLE);
    progress_blink(); // Checkpoint 6: Video stream routed

    /* 9. Output System Information and Status to Terminal */
    DBG_LOG("System online!");
    DBG_LOG("SYSCLK = %lu Hz", HAL_RCC_GetSysClockFreq());
    DBG_LOG("HCLK   = %lu Hz", HAL_RCC_GetHCLKFreq());
    DBG_LOG("PCLK1  = %lu Hz", HAL_RCC_GetPCLK1Freq());
    DBG_LOG("PCLK2  = %lu Hz", HAL_RCC_GetPCLK2Freq());
    DBG_LOG("LCD Init Result Status: %u", lcd_status);

    /* 10. Perform screen draw validation */
    fb[0] = 0xFF0000FF;         /* Blue pixel at (0,0) */
    fb[800 + 0] = 0xFFFF0000;    /* Red pixel at (1,0) */
    fb[400*800 + 200] = 0xFF00FF00; /* Green pixel at (200, 400) */
    progress_blink(); // Checkpoint 7: Raw writes to Framebuffer complete

    /* Clear foreground using high-performance hardware acceleration (DMA2D).
     * Stays visible as a boot indicator until LVGL's first full frame lands. */
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_FOREGROUND);
    BSP_LCD_Clear(LCD_COLOR_RED);
    progress_blink(); // Checkpoint 8: Display cleared to solid Red

    /* 11. LVGL UI — double-buffered LTDC display, touch indev, main screen */
    uint8_t ts_status = lvgl_display_init();
    ui_main_screen_create();
    progress_blink(); // Checkpoint 9: LVGL display + main screen ready
    DBG_LOG("LVGL UI online");
    DBG_LOG("Touch Init Status: %u", ts_status);

    uint8_t rx_byte;

    /* Main Superloop */
    while (1)
    {
        lv_timer_handler(); /* non-blocking — renders pending UI work */

        thermo_update(HAL_GetTick());   /* demo drift + mode LED, non-blocking */

        /* Retrieve bytes arriving over the VCP connection */
        if (HAL_UART_Receive(&huart3, &rx_byte, 1, 0) == HAL_OK)
        {
            uart_parser_feed_byte(get_uart_parser_ptr(), rx_byte);
        }
    }
}

static void MX_USART3_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable peripheral and GPIO bank clocks */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* * Configure Pins:
     * PB10 -> USART3_TX (Routed directly to ST-Link VCP)
     * PB11 -> USART3_RX
     */
    GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 115200;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart3);
}

void SystemClock_Config(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_OscInitTypeDef RCC_OscInitStruct;
    HAL_StatusTypeDef ret = HAL_OK;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
#if defined(USE_STM32469I_DISCO_REVA)
    RCC_OscInitStruct.PLL.PLLM = 25;
#else
    RCC_OscInitStruct.PLL.PLLM = 8;
#endif 
    RCC_OscInitStruct.PLL.PLLN = 360;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    RCC_OscInitStruct.PLL.PLLR = 6;

    ret = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    if(ret != HAL_OK)
    {
        while(1) { ; }
    }

    ret = HAL_PWREx_EnableOverDrive();
    if(ret != HAL_OK)
    {
        while(1) { ; }
    }

    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    ret = HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
    if(ret != HAL_OK)
    {
        while(1) { ; }
    }
}