# AGENTS.md — DIGITAL THERMOSTAT UI (STM32F469I-DISCO)

## Project type
PlatformIO embedded C for STM32F469NIH6 (Cortex-M4 @ 180 MHz, 2 MB Flash). Board in `platformio.ini` is `disco_f469ni`. External SDRAM @0xC0000000 — layout in `lv_conf.h`. Reference mockup: `mockup-ui.jpg` (no "ST" logo anywhere in the UI).

## Build & verify
```
pio run            # build firmware
pio run -t upload  # flash via ST-LINK (openocd)
pio run -t monitor # serial monitor @ 115200 baud
```

## Architecture
- **Display:** 800×480 DSI LCD (OTM8009A) via LTDC; LVGL software renderer draws directly into double-buffered SDRAM framebuffers (`-DLV_USE_DRAW_SW_ASM=0`; no DMA2D/Chrom-ART for LVGL despite SPECIFICATION.md §2 history)
- **Touch:** FT6206 on I2C1 (PB8/PB9, 400 kHz), polled by an LVGL pointer indev (`src/lvgl_display.c`); INT pin PJ5 unused (polling mode). If no controller is detected, `lvgl_display_init()` returns `TS_DEVICE_NOT_FOUND` and the UI still works via UART
- **Framework:** STM32Cube HAL (`framework = stm32cube`; framework provides `STM32F469xx`, `USE_HAL_DRIVER`, `F_CPU=180000000L`)
- **UI:** LVGL 9.5 (registry dep `lvgl/lvgl@^9.1.0`), ARGB8888/32bpp, 8 MB SDRAM heap @0xC0300000. Single thermostat screen: hero temperature display (generated ~180 px digit font in `src/ui_font_temp.c`, superscript °F from `src/ui_font_unit.c`), setpoint chevron buttons, mode bar with HEAT/AC/AUTO (mutually exclusive) + FAN as an independent on/off toggle (active = colored fill, inactive = flat). Icons are generated A8 alpha-mask images in `include/ui_icons.h`, recolored per state. LVGL 9 event filters are exact matches — never OR multiple `LV_EVENT_*` codes in one `lv_obj_add_event_cb()` call
- **Comms:** UART3 @ 115200 baud on PB10/PB11 → ST-LINK VCP (debug out now; future host link per SPECIFICATION.md §5 — parser + device-side setters for `0x50`/`0x51`/`0x53`/`0x54` implemented, `0x52` GET_STATE and the host daemon not yet started)
- **State model:** `src/thermostat_state.c` — mode (HEAT/AC/AUTO) + independent fan toggle + current temp + setpoint with a light demo simulation (temp drifts toward setpoint in HEAT/AC). Onboard LEDs mirror the active mode (+ orange when fan on) for debug; `thermo_update()` rewrites those pins every loop, so don't drive them from elsewhere
- **BSP:** LCD/SDRAM/TS drivers are vendored in `lib/stm32_bsp` (local lib in `lib_deps`, not the ST registry package). I2C1 for touch is lazily initialized inside the BSP (`TS_IO_Init()` → `I2C1_Init()`); no `MX_I2C1_Init` needed in `main.c`

Full spec: `SPECIFICATION.md` — read before generating UI screens, packet parsers, or state machines.

## Source layout
```
src/main.c              — HAL init, UART3 (PB10/PB11), boot checkpoints (LED_ORANGE toggled per stage), main superloop; SysTick_Handler feeds lv_tick_inc()
src/thermostat_state.c  — Thermostat state model + demo simulation, non-blocking UART parser, dbg_print() → HAL_UART_Transmit(USART3) is the debug path
src/lvgl_display.c      — LVGL port: direct-mode LTDC display, flush/swap protocol (LTDC_IRQHandler), FT6206 touch indev
src/ui_main_screen.c    — Thermostat screen: hero temp label, setpoint chevrons, mode bar; restyles on state change
src/ui_font_temp.c      — GENERATED ~180 px digit font (lv_font_conv); do not hand-edit
src/ui_font_unit.c      — GENERATED ~108 px "°F" unit font (lv_font_conv); do not hand-edit
include/thermostat_state.h — Types, command IDs, packet framing constants, public API
include/ui_icons.h      — GENERATED icon images (tools/gen_icons.py); do not hand-edit
lib/stm32_bsp/          — Vendored ST BSP: stm32469i_discovery_{lcd,sdram,ts}.c + Components/ft6x06 driver
lv_conf.h               — LVGL config; SDRAM layout (FB1 @0xC0000000, FB2 @0xC0180000, 8 MB heap @0xC0300000)
tools/gen_icons.py      — Regenerates include/ui_icons.h (needs Pillow: pip install pillow)
```

## Critical constraints (agents miss these)
- **`-DHSE_VALUE=8000000UL` is required** (in `platformio.ini`). This board's HSE crystal is 8 MHz; the framework hardcodes 25 MHz, which stretches every HAL tick/timeout ×3.125 in wall time and makes `HAL_DSI_Init` write an out-of-spec DSI PHY bit period. Removing it silently regresses boot timing (`SystemClock_Config()` uses PLLM=8 for non-REVA).
- **`LV_USE_FONT_COMPRESSED 1` is required** (in `lv_conf.h`). lv_font_conv emits RLE-compressed glyph data (`bitmap_format = LV_FONT_FMT_TXT_COMPRESSED`); without the flag every generated-font glyph decodes to NULL and labels render blank.
- **Boot order matters:** init UART3 right after `SystemClock_Config()` (BRR is computed from PCLK1) but *before* SDRAM/LCD init, so all boot phases are observable; set `DSI_WCR_LTDCEN` before `BSP_LCD_InitEx()` — `HAL_DSI_DeInit/Init` do not clear WCR, which is why it survives the BSP's internal re-init.
- **Never use `HAL_Delay()`** in main loops (boot phase may). Use `HAL_GetTick()`, non-blocking interrupts, or timer callbacks.
- **No `malloc` inside embedded loops.** Static allocation only — UI object pointers are module statics.
- **Frame buffers live in external SDRAM** (two full 800×480×4B buffers); the active one is swapped on LTDC refresh — see `src/lvgl_display.c`. Never assume they fit in internal RAM.
- **LVGL tick:** fed from `SysTick_Handler()` (main.c), not the main loop.
- **UART packet format:** `[0x02] [CMD_ID: 1B] [LEN: 1B] [DATA...] [END: 0x03]`. Command IDs in `include/thermostat_state.h` and `SPECIFICATION.md` §5. The parser dispatches on data length and never validates the END byte.
- **Onboard LEDs:** `thermo_update()` rewrites all four onboard LED pins every loop iteration (mode → color). Don't drive those pins from UI code.

## Future work (not stubs — not started)
- Real host link: `0x52` GET_STATE TX response + Python host daemon; disable the demo simulation per field as real telemetry arrives
- WS2812B shelf strip driver (SPI DMA or TIM PWM) — mode-colored ambience
- °C/°F unit toggle, schedules/tabs, setpoint persistence

## Onboard LEDs (for debug, not the shelf strip)
| LED | Pin    | Color  |
|-----|--------|--------|
| LD1 | PG6    | Green  |
| LD2 | PD4    | Orange |
| LD3 | PD5    | Red    |
| LD4 | PK3    | Blue   |

## VS Code / IDE
- Extension: `platformio.platformio-ide` (not cpptools-extension-pack)
- `.vscode/c_cpp_properties.json` is auto-generated by PlatformIO — do not hand-edit it.
