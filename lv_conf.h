/**
 * LVGL configuration for STM32F469I-DISCO (800×480 RGB LCD)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 1 = Use all required libraries; or set to 0 for selective features */
#define LV_USE_TASKHANDLER      1
#define LV_USE_LOG              0
#define LV_USE_ASSERT_MEM_INTEGRITY   0
#define LV_USE_ASSERT_STR_INTEGRITY   0
#define LV_USE_ASSERT_STYLE       0

/* 0: system default, 1: override, 2: use lv_conf_internal.h only */
#define LV_CONF_SUPPRESS_DEFINE_CHECK  1

/* Tick source — updated by timer interrupt in real firmware */
#ifndef LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM     0
#endif

/* Memory configuration — external SDRAM on STM32F469 */
#define LV_MEM_SIZE    (8UL * 1024 * 1024)   /* 8 MB SDRAM */

/* Color depth: RGB565 for 16-bit display bus */
#define LV_COLOR_DEPTH     16

/* Display resolution */
#define LV_HOR_RES_MAX     800
#define LV_VER_RES_MAX     480

/* Flush interface — double-buffered DMA in real firmware */
#define LV_USE_FLUSH       1
#define LV_DISP_DEF_REFR_PERIOD   30    /* ~33 FPS target */

/* Partial refresh mode (for scrolling areas) */
#define LV_USE_FULL_REFR      0

/* Use Chrom-ART / DMA2D acceleration if available */
#define LV_USE_GPU_STM32_DMA2D   1
#if LV_USE_GPU_STM32_DMA2D
    #define LV_GPU_DMA2D_CMSIS_INCLUDE "stm32f4xx_hal.h"
#endif

/* Enable basic widgets needed for the UI screens */
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
