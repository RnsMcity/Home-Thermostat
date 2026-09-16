#ifndef UI_MAIN_SCREEN_H
#define UI_MAIN_SCREEN_H

/* Create the thermostat main screen (SPECIFICATION.md §3). Call after
 * lvgl_display_init(). The screen reads its state from thermostat_state.c;
 * a slow LVGL timer keeps widgets in sync with demo drift / UART changes. */
void ui_main_screen_create(void);

#endif /* UI_MAIN_SCREEN_H */
