#include "ui_main_screen.h"
#include "lvgl.h"
#include "thermostat_state.h"
#include "ui_icons.h"

#include <stdio.h>

LV_FONT_DECLARE(ui_font_temp_180);
LV_FONT_DECLARE(ui_font_unit_108);

/* UI log helper — same pattern as DBG_LOG in thermostat_state.c */
#define UI_LOG(fmt, ...) do { \
    char _ui_buf[96]; \
    snprintf(_ui_buf, sizeof(_ui_buf), "[UI] " fmt "\r\n", ##__VA_ARGS__); \
    dbg_print(_ui_buf); \
} while(0)

/* ---------------------------------------------------------------------------
 * Layout constants (800x480 canvas — SPECIFICATION.md §3, measured from
 * mockup-ui.jpg)
 * --------------------------------------------------------------------------- */
#define CANVAS_W          800
#define CANVAS_H          480

#define TOP_PANEL_BG      lv_color_hex(0xE4E3E8)
#define MODE_BAR_Y        320
#define MODE_BAR_H        (CANVAS_H - MODE_BAR_Y)
#define MODE_BAR_BG       lv_color_hex(0xCBCACF)

#define TEXT_DARK         lv_color_hex(0x3A3A3A)
#define ICON_INACTIVE     lv_color_hex(0x45474B)
#define ARROW_BTN_BG      lv_color_hex(0xD2D3D7)

/* Hero display (digit cap top lands at ~y=122 with the generated font's
 * compact metrics: line_height 129, base_line 1) */
#define LBL_CURRENT_X     -30    /* "Current" centered at x=370 */
#define LBL_CURRENT_Y     40
#define HERO_X            -17    /* digit+°F block centered at x=383 */
#define HERO_Y            120

/* Setpoint column */
#define SP_BTN_W          96
#define SP_BTN_H          96
#define SP_BTN_RADIUS     18
#define SP_BTN_X          610
#define SP_UP_Y           36
#define SP_DOWN_Y         200
#define SP_LBL_CX         658    /* setpoint label center */
#define SP_LBL_CY         168

/* Mode buttons: x = MODE_BTN_X0 + i * MODE_BTN_PITCH */
#define MODE_BTN_W        152
#define MODE_BTN_H        132
#define MODE_BTN_Y        334
#define MODE_BTN_X0       72
#define MODE_BTN_PITCH    168

/* Active background per mode (SPECIFICATION.md §3.2) */
static const uint32_t s_mode_active_bg[4] = {
    0x880D08,   /* HEAT */
    0x26699D,   /* AC   */
    0x2F8F83,   /* FAN  */
    0xB07D2E,   /* AUTO */
};

static const char * s_mode_names[4] = { "HEAT", "AC", "FAN", "AUTO" };

/* Button index -> mode. FAN (index 2) is an independent on/off toggle, not a
 * mode — it maps to -1 and is handled separately in on_mode_click(). */
#define FAN_BTN_IDX   2
static const int8_t s_btn_mode[4] = { MODE_HEAT, MODE_AC, -1, MODE_AUTO };

/* ---------------------------------------------------------------------------
 * Module state (static allocation only — no malloc in embedded loops)
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t * btn;
    lv_obj_t * img;
    lv_obj_t * lbl;
    bool      active;   /* last applied style state, to avoid restyle churn  */
} mode_btn_t;

static lv_obj_t   * s_lbl_temp;       /* hero digits                          */
static lv_obj_t   * s_lbl_setpoint;   /* "74°F" between the chevron buttons   */
static mode_btn_t   s_modes[4];       /* HEAT, AC, FAN (toggle), AUTO         */
static lv_timer_t * s_sync_timer;

/* Cached state — sync only rewrites widgets when something actually changed */
static int16_t s_last_temp = -1;
static int16_t s_last_sp   = -1;

/* ---------------------------------------------------------------------------
 * State -> widget sync
 * --------------------------------------------------------------------------- */

static void mode_btn_apply(uint8_t idx, bool active)
{
    mode_btn_t * mb = &s_modes[idx];

    if (active) {
        lv_obj_set_style_bg_color(mb->btn, lv_color_hex(s_mode_active_bg[idx]), 0);
        lv_obj_set_style_bg_opa(mb->btn, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(mb->img, lv_color_white(), 0);
        lv_obj_set_style_text_color(mb->lbl, lv_color_white(), 0);
    } else {
        /* Inactive buttons are flat — no fill (SPECIFICATION.md §3.2) */
        lv_obj_set_style_bg_opa(mb->btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_image_recolor(mb->img, ICON_INACTIVE, 0);
        lv_obj_set_style_text_color(mb->lbl, ICON_INACTIVE, 0);
    }
    /* A8 icons render as a mask filled with the recolor — always set it */
    lv_obj_set_style_image_recolor_opa(mb->img, LV_OPA_COVER, 0);
}

static void ui_sync_now(void)
{
    thermo_mode_t mode = thermo_get_mode();
    int16_t temp       = thermo_get_current_temp_f();
    int16_t sp         = thermo_get_setpoint_f();

    if (temp != s_last_temp) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", temp);
        lv_label_set_text(s_lbl_temp, buf);
        s_last_temp = temp;
    }

    if (sp != s_last_sp) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d°F", sp);
        lv_label_set_text(s_lbl_setpoint, buf);
        s_last_sp = sp;
    }

    bool fan = thermo_get_fan();

    for (uint8_t i = 0; i < 4; i++) {
        bool active = (i == FAN_BTN_IDX) ? fan : (s_btn_mode[i] == mode);
        if (active != s_modes[i].active) {
            mode_btn_apply(i, active);
            s_modes[i].active = active;
        }
    }
}

/* Picks up the demo drift and any UART-driven state changes */
static void sync_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    ui_sync_now();
}

/* ---------------------------------------------------------------------------
 * Button handlers — single source of truth is thermostat_state.c
 * --------------------------------------------------------------------------- */

static void on_mode_click(lv_event_t * e)
{
    mode_btn_t * mb = lv_event_get_user_data(e);
    uint8_t idx = (uint8_t)(mb - s_modes);

    if (idx == FAN_BTN_IDX) {
        thermo_set_fan(!thermo_get_fan());   /* independent on/off toggle */
    } else {
        thermo_set_mode((thermo_mode_t)s_btn_mode[idx]);
    }
    ui_sync_now();   /* immediate visual feedback */
}

static void on_sp_up(lv_event_t * e)
{
    (void)e;
    thermo_adjust_setpoint(+THERMO_SETPOINT_STEP);
    ui_sync_now();
}

static void on_sp_down(lv_event_t * e)
{
    (void)e;
    thermo_adjust_setpoint(-THERMO_SETPOINT_STEP);
    ui_sync_now();
}

/* ---------------------------------------------------------------------------
 * Widget helpers
 * --------------------------------------------------------------------------- */

static lv_obj_t * flat_container(lv_obj_t * parent, uint32_t w, uint32_t h)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /* Flatten any theme decoration; zero padding so child alignment is
     * relative to the full container bounds */
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void button_flatten(lv_obj_t * btn)
{
    /* Zero padding so child alignment is relative to the full button bounds */
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void icon_setup(lv_obj_t * img, const lv_image_dsc_t * src, uint32_t px)
{
    lv_image_set_src(img, src);
    lv_obj_set_size(img, px, px);
    /* Scale the 64x64 source down to `px` while keeping aspect ratio */
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void ui_main_screen_create(void)
{
    lv_obj_t * scr = lv_screen_active();

    /* Top panel background for the whole screen */
    lv_obj_set_style_bg_color(scr, TOP_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Mode bar strip along the bottom */
    lv_obj_t * bar = flat_container(scr, CANVAS_W, MODE_BAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, MODE_BAR_Y);
    lv_obj_set_style_bg_color(bar, MODE_BAR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    /* "Current" label */
    lv_obj_t * lbl_current = lv_label_create(scr);
    lv_label_set_text(lbl_current, "Current");
    lv_obj_set_style_text_font(lbl_current, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_current, TEXT_DARK, 0);
    lv_obj_align(lbl_current, LV_ALIGN_TOP_MID, LBL_CURRENT_X, LBL_CURRENT_Y);

    /* Hero display: [digits][°F] as a top-aligned flex row */
    lv_obj_t * hero = flat_container(scr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 10, 0);   /* gap between digits and °F */

    s_lbl_temp = lv_label_create(hero);
    lv_obj_set_style_text_font(s_lbl_temp, &ui_font_temp_180, 0);
    lv_obj_set_style_text_color(s_lbl_temp, TEXT_DARK, 0);
    lv_label_set_text(s_lbl_temp, "72");

    lv_obj_t * lbl_unit = lv_label_create(hero);
    lv_obj_set_style_text_font(lbl_unit, &ui_font_unit_108, 0);
    lv_obj_set_style_text_color(lbl_unit, TEXT_DARK, 0);
    lv_label_set_text(lbl_unit, "°F");

    lv_obj_align(hero, LV_ALIGN_TOP_MID, HERO_X, HERO_Y);

    /* Setpoint column: up chevron / label / down chevron */
    for (uint8_t i = 0; i < 2; i++) {
        lv_obj_t * btn = lv_button_create(scr);
        button_flatten(btn);
        lv_obj_set_size(btn, SP_BTN_W, SP_BTN_H);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, SP_BTN_X, i == 0 ? SP_UP_Y : SP_DOWN_Y);
        lv_obj_set_style_bg_color(btn, ARROW_BTN_BG, 0);
        lv_obj_set_style_radius(btn, SP_BTN_RADIUS, 0);

        lv_obj_t * img = lv_image_create(btn);
        icon_setup(img, i == 0 ? &ui_icon_chevron_up : &ui_icon_chevron_down, 48);
        lv_obj_center(img);
        /* A8 icons render as a mask filled with the recolor */
        lv_obj_set_style_image_recolor(img, ICON_INACTIVE, 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);

        /* LVGL 9 event filters are exact matches (no bitmasking), so each
         * code gets its own registration. */
        if (i == 0) {
            lv_obj_add_event_cb(btn, on_sp_up, LV_EVENT_SHORT_CLICKED, NULL);
            lv_obj_add_event_cb(btn, on_sp_up, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
        } else {
            lv_obj_add_event_cb(btn, on_sp_down, LV_EVENT_SHORT_CLICKED, NULL);
            lv_obj_add_event_cb(btn, on_sp_down, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
        }
    }

    s_lbl_setpoint = lv_label_create(scr);
    lv_label_set_text(s_lbl_setpoint, "74°F");
    lv_obj_set_style_text_font(s_lbl_setpoint, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_setpoint, TEXT_DARK, 0);
    lv_obj_align(s_lbl_setpoint, LV_ALIGN_CENTER, SP_LBL_CX - CANVAS_W / 2, SP_LBL_CY - CANVAS_H / 2);

    /* Mode bar buttons: icon above label */
    static const lv_image_dsc_t * mode_icons[4] = {
        &ui_icon_flame, &ui_icon_snowflake, &ui_icon_fan, &ui_icon_auto
    };

    for (uint8_t i = 0; i < 4; i++) {
        s_modes[i].btn = lv_button_create(bar);
        button_flatten(s_modes[i].btn);
        lv_obj_set_size(s_modes[i].btn, MODE_BTN_W, MODE_BTN_H);
        lv_obj_align(s_modes[i].btn, LV_ALIGN_TOP_LEFT,
                     MODE_BTN_X0 + i * MODE_BTN_PITCH, (MODE_BAR_H - MODE_BTN_H) / 2);

        s_modes[i].img = lv_image_create(s_modes[i].btn);
        icon_setup(s_modes[i].img, mode_icons[i], 56);
        lv_obj_align(s_modes[i].img, LV_ALIGN_TOP_MID, 0, 24);   /* center ~y+52 */

        s_modes[i].lbl = lv_label_create(s_modes[i].btn);
        lv_label_set_text(s_modes[i].lbl, s_mode_names[i]);
        lv_obj_set_style_text_font(s_modes[i].lbl, &lv_font_montserrat_32, 0);
        lv_obj_align(s_modes[i].lbl, LV_ALIGN_TOP_MID, 0, MODE_BTN_H - 48);

        lv_obj_add_event_cb(s_modes[i].btn, on_mode_click, LV_EVENT_SHORT_CLICKED, &s_modes[i]);
    }

    /* Initial paint + slow sync for demo drift / UART changes */
    ui_sync_now();
    s_sync_timer = lv_timer_create(sync_timer_cb, 250, NULL);

    UI_LOG("Thermostat screen created");
}
