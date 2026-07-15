#include "stm32f4xx_hal.h"


UART_HandleTypeDef huart3;


static void LED_Init(void);
static void MX_USART3_UART_Init(void);


void SysTick_Handler(void)
{
    HAL_IncTick();
}


static void delay_loop(volatile uint32_t count)
{
    while (count--)
    {
        __NOP();
    }
}


int main(void)
{
    HAL_Init();


    LED_Init();

    MX_USART3_UART_Init();


    uint8_t msg[] = "ABC\r\n";


    while (1)
    {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_6);


        HAL_UART_Transmit(
            &huart3,
            msg,
            sizeof(msg)-1,
            100
        );


        delay_loop(10000000);
    }
}


static void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};


    /*
     * STM32F469I-DISCO MB1189-B01
     * User green LED = PG6
     */

    __HAL_RCC_GPIOG_CLK_ENABLE();


    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;


    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
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