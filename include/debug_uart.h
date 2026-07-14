#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include <stdio.h>
#include "stm32f4xx_hal.h"

/* USART3 on PD8 (TX) / PD9 (RX), 115200-8-N-1
 * Wired to ST-LINK's built-in USB-to-UART bridge → /dev/ttyACM* */
extern UART_HandleTypeDef huart3;

void debug_uart_init(void);
int  debug_uart_putchar(int ch, FILE *stream);

#endif /* DEBUG_UART_H */
