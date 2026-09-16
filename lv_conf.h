/**
 * LVGL configuration for STM32F469I-DISCO (800x480 DSI LCD via LTDC)
 * Target: LVGL v9.x (installed 9.5)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Color depth — must match the LTDC layer pixel format (ARGB8888).
 * The OTM8009A panel runs in RGB888 mode and both LTDC layers are configured
 * as ARGB8888 by the BSP, so LVGL renders 32-bit pixels directly.
 * ------------------------------------------------------------------------- */
#define LV_COLOR_DEPTH     32

/* ---------------------------------------------------------------------------
 * Memory pool — lives in external SDRAM (16 MB @ 0xC0000000).
 * Layout:
 *   0xC0000000  framebuffer 1 (active LTDC layer)   800*480*4 = 1,536,000 B
 *   0xC0180000  framebuffer 2 (back buffer)         1,536,000 B
 *   0xC0300000  LVGL heap (this pool)               8 MB -> ends 0xC0B00000
 * ------------------------------------------------------------------------- */
#define LV_MEM_SIZE    (8UL * 1024 * 1024)
#define LV_MEM_ADR     0xC0300000U

/* ---------------------------------------------------------------------------
 * Display driver — implemented in src/lvgl_display.c on top of the raw HAL
 * LTDC API (direct render mode, double-buffered SDRAM framebuffers). The
 * stock LV_USE_ST_LTDC driver is not used because it requires
 * USE_HAL_LTDC_REGISTER_CALLBACKS=1, which this framework's hal_conf.h does
 * not enable.
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Fonts — enable sizes as needed. The theme uses 14; the title uses 28.
 * ------------------------------------------------------------------------- */
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_28   1

/* ---------------------------------------------------------------------------
 * Widgets needed for the UI screens (SPECIFICATION.md section 3)
 * ------------------------------------------------------------------------- */
#define LV_USE_ARC        1
#define LV_USE_BTN        1
#define LV_USE_CHART      1
#define LV_USE_LABEL      1
#define LV_USE_LIST       1
#define LV_USE_SLIDER     1
#define LV_USE_IMG        1
#define LV_USE_LINE       1
#define LV_USE_METER      1
#define LV_USE_TABVIEW    1
#define LV_USE_COLORWHEEL 1

#endif /* LV_CONF_H */
