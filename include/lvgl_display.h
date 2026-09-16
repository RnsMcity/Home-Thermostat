#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

#include "lvgl.h"

/* Initialize LVGL, the double-buffered LTDC display, and (if present) the
 * FT6206 touch input device. Must be called after BSP_LCD_InitEx() and the
 * layer setup in main(). Returns the touch init status: TS_OK or
 * TS_DEVICE_NOT_FOUND (see stm32469i_discovery_ts.h). */
uint8_t lvgl_display_init(void);

#endif /* LVGL_DISPLAY_H */
