#include "ui_main_screen.h"
#include "lvgl.h"
#include "workspace_states.h"
#include "stm32469i_discovery.h"

#include <stdio.h>
#include <string.h>

/* UI log helper — same pattern as DBG_LOG in workspace_states.c */
#define UI_LOG(fmt, ...) do { \
    char _ui_buf[96]; \
    snprintf(_ui_buf, sizeof(_ui_buf), "[UI] " fmt "\r\n", ##__VA_ARGS__); \
    dbg_print(_ui_buf); \
} while(0)

/* ---------------------------------------------------------------------------
 * Module state (static allocation only — no malloc in embedded loops)
 * --------------------------------------------------------------------------- */
static lv_obj_t   * s_btn_pulse;      /* Button 1: "Pulse LEDs"            */
static lv_obj_t   * s_btn_state;      /* Button 2: shows current state     */
static lv_obj_t   * s_lbl_state;      /* Label inside button 2             */
static lv_timer_t * s_auto_demo_timer;/* One-shot, deletes itself          */
static lv_timer_t * s_led_pulse_timer;/* Repeating while pulsing           */
static uint8_t      s_led_pulses;     /* Toggles done in current pulse     */

/* ---------------------------------------------------------------------------
 * Button 1 — Pulse LEDs
 * --------------------------------------------------------------------------- */

static void led_pulse_cb(lv_timer_t * timer)
{
    BSP_LED_Toggle(LED_ORANGE);
    if (++s_led_pulses >= 3) {
        s_led_pulse_timer = NULL;
        lv_timer_delete(timer);
    }
}

static void on_btn_pulse(lv_event_t * e)
{
    (void)e;

    if (s_led_pulse_timer != NULL) return; /* already pulsing */

    /* Pause the state machine's LED pattern so it doesn't fight the pulse. */
    ui_set_led_override(700);
    s_led_pulses = 0;
    BSP_LED_On(LED_ORANGE);
    s_led_pulse_timer = lv_timer_create(led_pulse_cb, 200, NULL);

    UI_LOG("Pulse LEDs pressed");
}

/* ---------------------------------------------------------------------------
 * Button 2 — State display
 * --------------------------------------------------------------------------- */

static const char * state_name(system_state_t s)
{
    switch (s) {
        case STATE_MEETING:   return "MEETING";
        case STATE_CELEBRATE: return "CELEBRATE";
        default:              return "IDLE";
    }
}

static void on_btn_state(lv_event_t * e)
{
    (void)e;
    UI_LOG("State button pressed — state=%s", state_name(get_current_state()));
}

/* Keep button 2's label in sync with the state machine (10 Hz poll). */
static void state_poll_cb(lv_timer_t * timer)
{
    (void)timer;

    const char * name = state_name(get_current_state());
    if (strcmp(lv_label_get_text(s_lbl_state), name) != 0) {
        lv_label_set_text(s_lbl_state, name);
    }
}

/* ---------------------------------------------------------------------------
 * Auto-demo — one-shot press ~2 s after boot (no touch driver yet)
 * --------------------------------------------------------------------------- */

static void auto_demo_cb(lv_timer_t * timer)
{
    (void)timer;
    s_auto_demo_timer = NULL;
    lv_timer_delete(timer);

    UI_LOG("Auto-demo: pressing Pulse LEDs");
    ui_press_button(BTN_IDX_PULSE_LEDS);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void ui_main_screen_create(void)
{
    lv_obj_t * scr = lv_screen_active();

    /* Dark background for the whole screen */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "DESK COMMANDER");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8ECF4), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    /* Button 1 — Pulse LEDs */
    s_btn_pulse = lv_button_create(scr);
    lv_obj_set_size(s_btn_pulse, 260, 84);
    lv_obj_align(s_btn_pulse, LV_ALIGN_CENTER, -150, 0);
    lv_obj_t * lbl_pulse = lv_label_create(s_btn_pulse);
    lv_label_set_text(lbl_pulse, "Pulse LEDs");
    lv_obj_center(lbl_pulse);
    lv_obj_add_event_cb(s_btn_pulse, on_btn_pulse, LV_EVENT_SHORT_CLICKED, NULL);

    /* Button 2 — current state machine state */
    s_btn_state = lv_button_create(scr);
    lv_obj_set_size(s_btn_state, 260, 84);
    lv_obj_align(s_btn_state, LV_ALIGN_CENTER, 150, 0);
    s_lbl_state = lv_label_create(s_btn_state);
    lv_label_set_text(s_lbl_state, state_name(get_current_state()));
    lv_obj_center(s_lbl_state);
    lv_obj_add_event_cb(s_btn_state, on_btn_state, LV_EVENT_SHORT_CLICKED, NULL);

    /* Poll the state machine so button 2's label stays current */
    lv_timer_create(state_poll_cb, 100, NULL);

    /* One-shot auto-demo press shortly after boot */
    s_auto_demo_timer = lv_timer_create(auto_demo_cb, 2000, NULL);

    UI_LOG("Main screen created");
}

void ui_press_button(uint8_t idx)
{
    lv_obj_t * btn = NULL;

    if (idx == BTN_IDX_PULSE_LEDS) {
        btn = s_btn_pulse;
    } else if (idx == BTN_IDX_STATE) {
        btn = s_btn_state;
    }

    if (btn != NULL) {
        /* Same event a real touch release would generate */
        lv_obj_send_event(btn, LV_EVENT_SHORT_CLICKED, NULL);
    }
}
