# SYSTEM SPECIFICATION: DIGITAL THERMOSTAT UI
# Target Hardware: STM32F469I-DISCO
# UI Framework: LVGL 9.x (software renderer, ARGB8888)
# UI Screenshot: mockup-ui.jpg (framebuffer capture of the live UI)

## 1. SYSTEM ARCHITECTURE OVERVIEW
The system is a touch-operated digital thermostat front-end running on an STM32F469I-DISCO.
The firmware owns the 800x480 DSI LCD (LVGL), capacitive touch input, and a local
thermostat state model (mode + current temperature + setpoint).

There is no onboard Wi-Fi or sensor hardware yet: in this phase every displayed value
comes from a built-in demo simulation. A host daemon (Python) will later bridge real
sensor data / HVAC control to the STM32 over UART; the packet framing and non-blocking
parser already exist, only the command set changes (Section 5).

```
+---------------------------------------+
| Host (future): Python daemon          |
|   [UART @ 115200 baud - ST-LINK VCP]  |
v---------------------------------------+
| STM32F469I-DISCO                      |
|   +-------------------------------+   |
|   | LVGL UI (800x480)             |   |
|   | - Hero temperature display    |   |
|   | - Setpoint up/down controls   |   |
|   | - Mode bar: HEAT/AC/FAN/AUTO  |   |
|   +-------------------------------+   |
|   Thermostat state model (demo sim)   |
+---------------------------------------+
```

## 2. FIRMWARE & HARDWARE CONSTRAINTS (STM32)
* **Display:** 800x480 DSI LCD (OTM8009A) via LTDC, ARGB8888. LVGL's software renderer
  draws directly into double-buffered SDRAM framebuffers; no DMA2D/Chrom-ART for LVGL.
* **Memory:** Framebuffers and the 8 MB LVGL heap live in external SDRAM @0xC0000000
  (layout in `lv_conf.h`). UI assets (fonts, icons) are compiled into flash; runtime
  objects use static allocation only - no `malloc` inside loops.
* **Peripherals:**
  * UART3 @ 115200 baud on PB10/PB11 -> ST-LINK VCP (host link + debug output).
  * I2C1 for the FT6206 touch controller (polled by an LVGL pointer indev).
  * SysTick feeds `lv_tick_inc()`.
* **Clocks:** This board's HSE crystal is 8 MHz. `-DHSE_VALUE=8000000UL` in
  `platformio.ini` is mandatory - the framework default (25 MHz) stretches every HAL
  tick and breaks DSI PHY timing.

## 3. LVGL UI LAYER ARCHITECTURE (800x480 Canvas)
Single screen, no tabs or navigation. Reference: `mockup-ui.jpg`.
**No "ST" logo anywhere in the UI.**

### 3.1 Top Panel (y 0-320, background #E4E3E8)
All text color: #3A3A3A.

* **`lbl_current`:** text "Current", Montserrat 36, centered at x=370, top y=45.
* **Hero display** (flex row container, gap 10 px, centered at x=383, top y=112):
  * `lbl_temp`: current temperature as a plain integer in the generated ~180 px font
    (`ui_font_temp`, digits only), color #3A3A3A. This is the visual centerpiece.
  * `lbl_unit`: "°F" in the generated ~108 px font (`ui_font_unit`), top-aligned with
    the digits (superscript style, as in the mockup).
* **Setpoint column** (buttons at x=610, width 96; content centered at x=658):
  * `btn_setpoint_up`: 96x96 rounded button (radius 18), background #D2D3D7, chevron-up icon (48 px, color #45474B) centered, top y=36.
  * `lbl_setpoint`: setpoint as `"%d°F"`, Montserrat 36, centered at x=658, y=168.
  * `btn_setpoint_down`: identical to up with a chevron-down icon, top y=200.

### 3.2 Mode Bar (y 320-480, background #CBCACF)
Four equal mode buttons: 152x132 px at x = 72 / 240 / 408 / 576, top y=334
(vertically centered in the bar). Each button shows a 56x56 icon (center ~y+52) above
a Montserrat 32 label (center ~y+104).

| Button | Icon      | Active background |
|--------|-----------|-------------------|
| HEAT   | flame     | #880D08           |
| AC     | snowflake | #26699D           |
| FAN    | fan       | #2F8F83           |
| AUTO   | auto      | #B07D2E           |

* **Active** button: mode-specific background fill, icon and label white (#FFFFFF).
* **Inactive** buttons are *flat*: no background fill (transparent), icon and label
  dark gray #45474B. (Matches mockup - only the active modes show a colored box.)
* Exactly one of HEAT/AC/AUTO is active at all times; default on boot is HEAT
  (matches mockup). FAN is an independent on/off toggle that can be combined with
  any mode — it shows its #2F8F83 fill whenever the fan is on, regardless of mode.

### 3.3 Behavior (this phase - demo)
* Defaults: current = 72°F, setpoint = 74°F, mode = HEAT.
* Chevron buttons adjust the setpoint by +/-1°F per press, clamped to [50, 90]°F.
  Long-press repeats at LVGL's default repeat rate after the long-press delay.
* Mode buttons switch the active mode and restyle the bar immediately; the FAN
  button toggles the fan on/off independently of the selected mode.
* **Demo simulation:** while mode is HEAT or AC, the current temperature moves toward
  the setpoint at ~0.1°F/s (6°F/min) and holds once within 0.5°F of it; AUTO holds
  steady and fan state never affects temperature. This keeps the screen alive without
  hardware and is replaced by real telemetry per Section 5.
* Onboard LEDs mirror the active mode for debug: HEAT=red, AC=blue, AUTO=green, plus
  orange whenever the fan is on (solid).

## 4. STATE MODEL
```c
typedef enum { MODE_HEAT, MODE_AC, MODE_AUTO } thermo_mode_t;

int16_t       current_temp_f; /* integer °F */
int16_t       setpoint_f;     /* integer °F, clamped to [50, 90] */
thermo_mode_t mode;           /* exactly one active at a time */
bool          fan_on;         /* independent of mode — any mode + fan on/off */
```
Public API (`include/thermostat_state.h`): `thermo_init()`, `thermo_update(now_ms)`,
`thermo_set_mode(m)`, `thermo_adjust_setpoint(delta)`, `thermo_set_fan(on)` /
`thermo_get_fan()`, plus accessors.

## 5. LIGHTWEIGHT SERIAL COMMUNICATION PROTOCOL (future host link)
The Python daemon sanitizes sensor/HVAC data into deterministic byte streams so the
STM32 never parses JSON. Framing is unchanged from the previous revision:

`[START_BYTE: 0x02] [COMMAND_ID: 1 Byte] [DATA_LEN: 1 Byte] [DATA_BYTES] [END_BYTE: 0x03]`

Command IDs (defined in `thermostat_state.h`). Device-side handlers exist for
0x50/0x51/0x53/0x54; 0x52 has no TX response path yet:
* **`0x50` (SET_CURRENT_TEMP):** payload int16 little-endian, tenths of °F. Overwrites the current temperature (demo drift continues until real telemetry replaces it).
* **`0x51` (SET_MODE):** payload 1 byte = `thermo_mode_t`.
* **`0x52` (GET_STATE):** no payload; device will reply `[mode:1B][current int16 LE tenths °F][setpoint int16 LE]` — not implemented yet.
* **`0x53` (SET_SETPOINT):** payload int16 little-endian, tenths of °F.
* **`0x54` (SET_FAN):** payload 1 byte, 0=off 1=on. Independent of mode.

When real telemetry arrives, the demo simulation is disabled for the affected field.

## 6. CODE IMPLEMENTATION REQUIREMENTS FOR AI VISUAL AGENTS
1. **No Blocking Code:** no `HAL_Delay()` in the main loop; use `HAL_GetTick()`, LVGL timers, or non-blocking UART polling.
2. **Static Allocation Only:** no `malloc` inside embedded loops; UI object pointers are module statics.
3. **Framebuffers live in SDRAM** - never assume they fit internal RAM.
4. **LVGL tick** is fed from `SysTick_Handler()`, not the main loop.
5. **Generated assets, do not hand-edit:** `src/ui_font_temp.c` + `src/ui_font_unit.c`
   (lv_font_conv output) and `include/ui_icons.h` (output of `tools/gen_icons.py`;
   regenerate with Pillow installed).
6. **No "ST" logo** anywhere in the UI.
