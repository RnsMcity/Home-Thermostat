#ifndef WORKSPACE_STATES_H
#define WORKSPACE_STATES_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * System states (Section 5 of SPECIFICATION.md)
 * --------------------------------------------------------------------------- */
typedef enum {
    STATE_IDLE,
    STATE_MEETING,
    STATE_CELEBRATE
} system_state_t;

/* ---------------------------------------------------------------------------
 * UART command IDs (Section 4.2-4.3 of SPECIFICATION.md)
 * --------------------------------------------------------------------------- */
#define CMD_CPU_METRIC      0x10U
#define CMD_GPU_METRIC      0x11U
#define CMD_NET_METRIC      0x12U
#define CMD_TEAMS_STATUS    0x20U
#define CMD_GITLAB_MR       0x30U

/* ---------------------------------------------------------------------------
 * Packet framing (Section 4.1 of SPECIFICATION.md)
 * [START: 0x02] [CMD_ID: 1B] [LEN: 1B] [DATA...] [END: 0x03]
 * --------------------------------------------------------------------------- */
#define PKT_START           0x02U
#define PKT_END             0x03U

/* ---------------------------------------------------------------------------
 * Telemetry data (Section 4.2 of SPECIFICATION.md)
 * --------------------------------------------------------------------------- */
typedef struct {
    uint8_t cpu_load;       /* 0-100 % */
    uint8_t cpu_temp;       /* °C */
    uint8_t gpu_load;       /* 0-100 % */
    uint8_t gpu_temp;       /* °C */
    int32_t net_speed_up;   /* bytes/s, fixed-point or raw */
    int32_t net_speed_down; /* bytes/s, fixed-point or raw */
} telemetry_data_t;

/* ---------------------------------------------------------------------------
 * Teams status (Section 4.3 of SPECIFICATION.md)
 * --------------------------------------------------------------------------- */
typedef enum {
    TEAMS_AVAILABLE = 0x01U,
    TEAMS_BUSY      = 0x02U,
    TEAMS_AWAY      = 0x03U
} teams_status_t;

/* ---------------------------------------------------------------------------
 * Notification queue (static allocation — no malloc in embedded loops)
 * --------------------------------------------------------------------------- */
#define NOTIF_QUEUE_MAX       16U
#define NOTIF_PAYLOAD_MAX     80U

typedef struct {
    char        text[NOTIF_PAYLOAD_MAX];
    uint32_t    timestamp;   /* HAL tick at arrival */
} notification_entry_t;

typedef struct {
    notification_entry_t entries[NOTIF_QUEUE_MAX];
    volatile uint16_t    head;
    volatile uint16_t    tail;
    volatile uint8_t     count;
} notif_queue_t;

/* ---------------------------------------------------------------------------
 * UART parser state machine (non-blocking)
 * --------------------------------------------------------------------------- */
typedef enum {
    PARSER_WAIT_START,
    PARSER_GET_CMD,
    PARSER_GET_LEN,
    PARSER_READ_DATA,
    PARSER_WAIT_END
} uart_parser_state_t;

typedef struct {
    uart_parser_state_t  state;
    uint8_t              cmd_id;
    uint8_t              expected_len;
    uint8_t              data_buf[128];   /* max practical payload */
    uint8_t              data_idx;
} uart_parser_t;

/* ---------------------------------------------------------------------------
 * LED colour helper (RGB888 packed for WS2812B)
 * --------------------------------------------------------------------------- */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

#define RGB(r, g, b)  ((rgb_color_t){ (r), (g), (b) })

/* ---------------------------------------------------------------------------
 * Public API — workspace_states.c
 * --------------------------------------------------------------------------- */

/* State machine core ------------------------------------------------------- */
void state_machine_init(void);
void state_machine_update(uint32_t now_ms);

/* Telemetry handlers ------------------------------------------------------- */
void handle_cpu_metric(uint8_t load, uint8_t temp);
void handle_gpu_metric(uint8_t load, uint8_t temp);
void handle_net_metric(int32_t speed_up, int32_t speed_down);

/* Teams / GitLab handlers -------------------------------------------------- */
void handle_teams_status(teams_status_t status);
void handle_gitlab_mr(const char *branch_name);

/* Notification queue ------------------------------------------------------- */
void notif_queue_init(notif_queue_t *q);
uint8_t  notif_queue_push(notif_queue_t *q, const char *text, uint32_t ts);
uint8_t  notif_queue_pop(notif_queue_t *q, char *out_buf, size_t out_size);

/* UART parser -------------------------------------------------------------- */
void uart_parser_init(uart_parser_t *p);
void uart_parser_feed_byte(uart_parser_t *p, uint8_t byte);

/* LED & UI hooks (mocked for compilation without hardware) ----------------- */
void set_shelf_leds_raw(const rgb_color_t *colors, uint16_t count);
void trigger_lvgl_ui_refresh(void);

/* Accessors ---------------------------------------------------------------- */
system_state_t get_current_state(void);
telemetry_data_t *get_telemetry_ptr(void);
notif_queue_t  *get_notif_queue_ptr(void);
uart_parser_t  *get_uart_parser_ptr(void);

#endif /* WORKSPACE_STATES_H */
