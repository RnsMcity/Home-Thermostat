#ifndef THERMOSTAT_STATE_H
#define THERMOSTAT_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * Thermostat modes (Section 4 of SPECIFICATION.md). FAN is NOT a mode — it's
 * an independent on/off toggle (thermo_set_fan / thermo_get_fan) that can be
 * combined with any mode.
 * --------------------------------------------------------------------------- */
typedef enum {
    MODE_HEAT = 0,
    MODE_AC,
    MODE_AUTO
} thermo_mode_t;

/* Setpoint limits and step (integer °F) */
#define THERMO_SETPOINT_MIN   50
#define THERMO_SETPOINT_MAX   90
#define THERMO_SETPOINT_STEP  1

/* ---------------------------------------------------------------------------
 * UART command IDs for the future host link (Section 5 of SPECIFICATION.md).
 * The parser accepts these now; GET_STATE has no TX path yet.
 * --------------------------------------------------------------------------- */
#define CMD_SET_CURRENT_TEMP  0x50U   /* payload: int16 LE, tenths of °F */
#define CMD_SET_MODE          0x51U   /* payload: 1 byte thermo_mode_t    */
#define CMD_GET_STATE         0x52U   /* no payload (not implemented)     */
#define CMD_SET_SETPOINT      0x53U   /* payload: int16 LE, tenths of °F  */
#define CMD_SET_FAN           0x54U   /* payload: 1 byte, 0=off 1=on      */

/* ---------------------------------------------------------------------------
 * Packet framing (Section 5 of SPECIFICATION.md)
 * [START: 0x02] [CMD_ID: 1B] [LEN: 1B] [DATA...] [END: 0x03]
 * --------------------------------------------------------------------------- */
#define PKT_START           0x02U
#define PKT_END             0x03U

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
 * Public API — thermostat_state.c
 * --------------------------------------------------------------------------- */

void thermo_init(void);
void thermo_update(uint32_t now_ms);      /* demo drift + mode LED, call every loop */

void thermo_set_mode(thermo_mode_t mode);
void thermo_adjust_setpoint(int16_t delta);  /* clamped to [MIN, MAX] */

/* Fan — independent of mode (Section 4) */
void thermo_set_fan(bool on);
bool thermo_get_fan(void);

/* Host-link setters (Section 5) — overwrite the demo values */
void thermo_set_current_temp_f(int16_t temp_f);
void thermo_set_setpoint_f(int16_t setpoint_f);

thermo_mode_t thermo_get_mode(void);
int16_t       thermo_get_current_temp_f(void);   /* rounded to whole °F */
int16_t       thermo_get_setpoint_f(void);

/* UART parser -------------------------------------------------------------- */
void uart_parser_init(uart_parser_t *p);
void uart_parser_feed_byte(uart_parser_t *p, uint8_t byte);
uart_parser_t *get_uart_parser_ptr(void);

/* Debug UART line printer (defined in thermostat_state.c) */
void dbg_print(const char *msg);

#endif /* THERMOSTAT_STATE_H */
