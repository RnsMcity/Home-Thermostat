#include "stm32f4xx_hal.h"
#include "workspace_states.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_sdram.h"


UART_HandleTypeDef huart3;


static void MX_USART3_UART_Init(void);


void SysTick_Handler(void)
{
    HAL_IncTick();
}


int main(void)
{
    uint8_t lcd_status, sdram_status;

    HAL_Init();

    MX_USART3_UART_Init();

    state_machine_init();

    /* Initialize SDRAM (framebuffer memory at 0xC0000000) */
    sdram_status = BSP_SDRAM_Init();

    /* Initialize LCD (DSI + LTDC + DMA2D + OTM8009A display IC) */
    lcd_status = BSP_LCD_Init();

    if (lcd_status == LCD_OK && sdram_status == SDRAM_OK)
    {
        /* Test pattern: fill with blue, draw colored rectangles and circles */
        BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_FOREGROUND);
        BSP_LCD_Clear(LCD_COLOR_BLUE);

        /* Top bar - red */
        BSP_LCD_SetTextColor(LCD_COLOR_RED);
        BSP_LCD_FillRect(0, 0, 800, 40);

        /* Bottom bar - green */
        BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
        BSP_LCD_FillRect(0, 440, 800, 40);

        /* Center circle - yellow */
        BSP_LCD_SetTextColor(LCD_COLOR_YELLOW);
        BSP_LCD_FillCircle(400, 240, 100);

        /* Inner circle - white */
        BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
        BSP_LCD_FillCircle(400, 240, 60);

        /* Text: "DESK COMMANDER" centered */
        BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
        BSP_LCD_SetFont(&Font24);
        BSP_LCD_DisplayStringAt(400, 230, (uint8_t *)"DESK", CENTER_MODE);
        BSP_LCD_DisplayStringAt(400, 260, (uint8_t *)"COMMANDER", CENTER_MODE);

        /* Status text */
        BSP_LCD_SetFont(&Font16);
        BSP_LCD_SetTextColor(LCD_COLOR_LIGHTGRAY);
        BSP_LCD_DisplayStringAt(400, 300, (uint8_t *)"LCD OK", CENTER_MODE);
    }
    else
    {
        /* Display error - fill with red */
        BSP_LCD_Clear(LCD_COLOR_RED);
        BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
        BSP_LCD_SetFont(&Font24);
        if (lcd_status != LCD_OK)
            BSP_LCD_DisplayStringAt(400, 200, (uint8_t *)"LCD FAIL", CENTER_MODE);
        else
            BSP_LCD_DisplayStringAt(400, 200, (uint8_t *)"SDRAM FAIL", CENTER_MODE);
    }

    uint8_t rx_byte;

    while (1)
    {
        state_machine_update(HAL_GetTick());

        /* Non-blocking UART poll — feed each available byte to parser */
        if (HAL_UART_Receive(&huart3, &rx_byte, 1, 0) == HAL_OK)
        {
            uart_parser_feed_byte(get_uart_parser_ptr(), rx_byte);
        }
    }
}


static void MX_USART3_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /*
     * STM32F469I-DISCO MB1189-B01
     *
     * PB10 -> USART3_TX -> ST-LINK VCP
     * PB11 -> USART3_RX
     */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();


    GPIO_InitStruct.Pin =
        GPIO_PIN_10 |
        GPIO_PIN_11;

    GPIO_InitStruct.Mode =
        GPIO_MODE_AF_PP;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_VERY_HIGH;

    GPIO_InitStruct.Alternate =
        GPIO_AF7_USART3;


    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);


    huart3.Instance = USART3;

    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart3.Init.StopBits =
        UART_STOPBITS_1;

    huart3.Init.Parity =
        UART_PARITY_NONE;

    huart3.Init.Mode =
        UART_MODE_TX_RX;

    huart3.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart3.Init.OverSampling =
        UART_OVERSAMPLING_16;


    HAL_UART_Init(&huart3);
}
