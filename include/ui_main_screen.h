#ifndef UI_MAIN_SCREEN_H
#define UI_MAIN_SCREEN_H

#include <stdint.h>

/* Button indices — match the CMD_UI_PRESS_BTN UART payload */
#define BTN_IDX_PULSE_LEDS   0U
#define BTN_IDX_STATE        1U

/* Create the main screen (title + buttons). Call after lvgl_display_init(). */
void ui_main_screen_create(void);

/* Simulate a press of button `idx`. Safe to call from the UART dispatch path
 * (main-loop context, same thread as lv_timer_handler()). No-op if the UI is
 * not up yet or the index is unknown. */
void ui_press_button(uint8_t idx);

#endif /* UI_MAIN_SCREEN_H */
