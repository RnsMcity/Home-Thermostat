#include "stm32f4xx_hal.h"
#include "workspace_states.h"


UART_HandleTypeDef huart3;


static void MX_USART3_UART_Init(void);


void SysTick_Handler(void)
{
    HAL_IncTick();
}


int main(void)
{
    HAL_Init();

    MX_USART3_UART_Init();

    state_machine_init();

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
