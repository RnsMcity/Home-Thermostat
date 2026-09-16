#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

#include "lvgl.h"

/* Initialize LVGL and create the double-buffered LTDC display.
 * Must be called after BSP_LCD_InitEx() and the layer setup in main(). */
void lvgl_display_init(void);

#endif /* LVGL_DISPLAY_H */
