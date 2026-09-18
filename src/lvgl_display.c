#include "lvgl_display.h"
#include "stm32f4xx_hal.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_ts.h"
#include "stm32469i_discovery.h"

/* SDRAM layout (16 MB @ 0xC0000000) — see lv_conf.h */
#define LVGL_FB1_ADDR   LCD_FB_START_ADDRESS               /* 0xC0000000, active layer FB */
#define LVGL_FRAME_SIZE (800U * 480U * 4U)                 /* full ARGB8888 frame          */
#define LVGL_FB2_ADDR   (LCD_FB_START_ADDRESS + 0x180000U) /* 0xC0180000, back buffer      */

/* Set when a layer-address swap has been requested and is waiting for the
 * LTDC register-reload-complete interrupt. */
static volatile bool s_swap_pending = false;

/* ---------------------------------------------------------------------------
 * Backlight / idle dimming — the OTM8009A drives its own LED backlight via
 * DSI register 0x51 (WRDISBV, 0x00..0xFF); there is no MCU-side PWM pin.
 * ------------------------------------------------------------------------- */
#define BACKLIGHT_FULL    0x7F   /* matches the level BSP init programs        */
#define BACKLIGHT_DIM     0x10   /* ~6% — still readable at a glance          */
#define DIM_TIMEOUT_MS    5000   /* idle time before dimming starts            */
#define FADE_PERIOD_MS    30     /* ramp timer period                          */
#define FADE_STEP         8      /* levels per tick → full↔dim in ~400 ms      */

static uint8_t  s_backlight = BACKLIGHT_FULL;
static uint32_t s_last_activity;   /* lv_tick ms of last touch                 */

/* Write the panel backlight register. Callers must only invoke on change —
 * DSI_IO_WriteCmd blocks briefly on the command lane. */
static void backlight_set(uint8_t level)
{
    uint8_t cmd[2] = { OTM8009A_CMD_WRDISBV, level };

    s_backlight = level;
    DSI_IO_WriteCmd(1, cmd);
}

/* Periodic ramp toward the brightness target for the current activity state. */
static void dim_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    uint8_t target = (lv_tick_elaps(s_last_activity) < DIM_TIMEOUT_MS)
                     ? BACKLIGHT_FULL : BACKLIGHT_DIM;

    if (s_backlight == target) return;

    if (s_backlight < target) {
        s_backlight += FADE_STEP;
        if (s_backlight > target) s_backlight = target;
    } else {
        s_backlight -= FADE_STEP;
        if (s_backlight < target) s_backlight = target;
    }
    backlight_set(s_backlight);
}

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
 * Touch input (FT6206 on I2C1, PB8/PB9) — polled by LVGL's indev timer.
 * ------------------------------------------------------------------------- */

static void ts_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    static TS_StateTypeDef ts_state;

    if (BSP_TS_GetState(&ts_state) == TS_OK && ts_state.touchDetected > 0) {
        s_last_activity = lv_tick_get();   /* any touch resets the idle timer */
        data->point.x = ts_state.touchX[0];
        data->point.y = ts_state.touchY[0];
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

/* ---------------------------------------------------------------------------
 * Init — returns the touch controller init status (TS_OK / TS_DEVICE_NOT_FOUND)
 * ------------------------------------------------------------------------- */

uint8_t lvgl_display_init(void)
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

    /* Touch input — BSP_TS_Init maps raw FT6206 coordinates to the 800x480
     * landscape display. Without a working controller the UI still works via
     * UART (CMD_UI_PRESS_BTN). */
    uint8_t ts_status = BSP_TS_Init(800, 480);
    if (ts_status == TS_OK) {
        lv_indev_t * indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, ts_read_cb);

        /* Idle dimming — only armed when touch can wake the display again.
         * The panel register already holds BACKLIGHT_FULL from BSP init, so
         * no initial write is needed. */
        s_last_activity = lv_tick_get();
        lv_timer_create(dim_timer_cb, FADE_PERIOD_MS, NULL);
    }

    return ts_status;
}
