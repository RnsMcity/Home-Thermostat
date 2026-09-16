#include "lvgl_display.h"
#include "stm32f4xx_hal.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery.h"

/* SDRAM layout (16 MB @ 0xC0000000) — see lv_conf.h */
#define LVGL_FB1_ADDR   LCD_FB_START_ADDRESS               /* 0xC0000000, active layer FB */
#define LVGL_FRAME_SIZE (800U * 480U * 4U)                 /* full ARGB8888 frame          */
#define LVGL_FB2_ADDR   (LCD_FB_START_ADDRESS + 0x180000U) /* 0xC0180000, back buffer      */

/* Set when a layer-address swap has been requested and is waiting for the
 * LTDC register-reload-complete interrupt. */
static volatile bool s_swap_pending = false;

/* ---------------------------------------------------------------------------
 * Flush callbacks — direct render mode with double buffering:
 * LVGL renders straight into one of the two SDRAM framebuffers. After the
 * last area of a refresh we point the LTDC layer at that buffer, effective
 * at vertical blanking (tear-free). Same protocol as LVGL's stock ST LTDC
 * driver, but reload synchronization uses a raw ISR flag instead of HAL
 * register callbacks (not enabled in this framework's hal_conf.h).
 * ------------------------------------------------------------------------- */

static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    (void)area;

    if (!lv_display_flush_is_last(disp)) return;

    HAL_LTDC_SetAddress_NoReload(&hltdc_eval, (uint32_t)px_map, LTDC_ACTIVE_LAYER_FOREGROUND);
    s_swap_pending = true;
    HAL_LTDC_Reload(&hltdc_eval, LTDC_RELOAD_VERTICAL_BLANKING);
}

static void lvgl_flush_wait_cb(lv_display_t * disp)
{
    (void)disp;

    /* Spin until the reload IRQ confirms the swap took effect. Bounded by one
     * vertical blanking period (~16 ms at panel refresh rate). */
    while (s_swap_pending) { }
}

/* ---------------------------------------------------------------------------
 * LTDC interrupt — vector table entry
 * ------------------------------------------------------------------------- */

void LTDC_IRQHandler(void)
{
    uint32_t isr = LTDC->ISR;

    if (isr & LTDC_ISR_RRIF) {
        LTDC->ICR = LTDC_ICR_CRRIF; /* write-1-to-clear */
        s_swap_pending = false;
    }
}

/* ---------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

void lvgl_display_init(void)
{
    lv_init();

    lv_display_t * disp = lv_display_create(800, 480);
    if (disp == NULL) {
        while (1) {
            BSP_LED_Toggle(LED_ORANGE);
            HAL_Delay(100);
        }
    }

    /* Default dark theme — without it buttons have no background style and
     * label text falls back to black, which is invisible on the dark UI. */
    lv_theme_t * th = lv_theme_default_init(disp, lv_color_hex(0x4C8BF5),
                                            lv_color_hex(0xF5A623), true /*dark*/,
                                            &lv_font_montserrat_14);
    lv_display_set_theme(disp, th);

    /* Match the LTDC layer pixel format (ARGB8888) */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    /* Two full-frame SDRAM buffers; direct render mode */
    lv_display_set_buffers(disp, (void *)LVGL_FB1_ADDR, (void *)LVGL_FB2_ADDR,
                           LVGL_FRAME_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_flush_wait_cb(disp, lvgl_flush_wait_cb);

    /* LVGL owns the foreground layer; hide the background one. */
    BSP_LCD_SetLayerVisible(LTDC_ACTIVE_LAYER_BACKGROUND, DISABLE);

    /* Clear stale flags and enable the register-reload interrupt (NVIC for
     * LTDC_IRQn is already enabled by BSP_LCD_MspInit()). The reload flag is
     * what tells us the buffer swap took effect at vertical blanking. */
    __HAL_LTDC_CLEAR_FLAG(&hltdc_eval, LTDC_FLAG_RR | LTDC_FLAG_TE | LTDC_FLAG_FU);
    s_swap_pending = false;
    __HAL_LTDC_ENABLE_IT(&hltdc_eval, LTDC_IT_RR);
}
