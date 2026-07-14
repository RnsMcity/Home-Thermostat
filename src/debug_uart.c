#include "debug_uart.h"

/* Forward declaration from main.c */
void Error_Handler(void);

/* ---------------------------------------------------------------------------
 * Global UART handle — used by HAL callbacks and main code
 * --------------------------------------------------------------------------- */
UART_HandleTypeDef huart3;

/* ---------------------------------------------------------------------------
 * USART3 TX/RX pins: PD8 (TX), PD9 (RX)
 * Wired to ST-LINK's built-in USB-to-UART bridge → /dev/ttyACM*
 * No external adapter needed.
 * --------------------------------------------------------------------------- */

static void debug_uart_gpio_init(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    /* Enable clocks for USART3 and GPIOD */
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PD8 → USART3_TX (alternate function AF7) */
    gpio.Pin   = GPIO_PIN_8;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* PD9 → USART3_RX (alternate function AF7) */
    gpio.Pin   = GPIO_PIN_9;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void debug_uart_init(void)
{
    debug_uart_gpio_init();

    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 115200;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart3) != HAL_OK) {
        /* If init fails, Error_Handler will halt */
        Error_Handler();
    }
}

/* ---------------------------------------------------------------------------
 * Redirect stdout/printf to USART3 via HAL_UART_Transmit
 * Called by newlib-nano's _write implementation
 * --------------------------------------------------------------------------- */

int debug_uart_putchar(int ch, FILE *stream)
{
    /* Send single byte (blocks briefly — acceptable for debug output) */
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* ---------------------------------------------------------------------------
 * newlib-nano _write override — routes all stdout to our UART
 * This is what makes printf/puts/fprintf(stdout) work
 * --------------------------------------------------------------------------- */

int _write(int fd, char *ptr, int len)
{
    (void)fd;  /* We only support stdout/stderr (fd=1,2) */
    for (int i = 0; i < len; i++) {
        debug_uart_putchar(*ptr++, NULL);
    }
    return len;
}
