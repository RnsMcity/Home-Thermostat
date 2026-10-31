# AGENTS.md — DIGITAL THERMOSTAT UI (STM32F469I-DISCO)

## Project type
PlatformIO embedded C for STM32F469NIH6 (Cortex-M4 @ 180 MHz, 2 MB Flash). Board in `platformio.ini` is `disco_f469ni`. External SDRAM @0xC0000000 — layout in `lv_conf.h`. `mockup-ui.jpg` is a framebuffer capture of the live UI (regenerate via openocd SDRAM dump + `tools/capture_screen.py` — see that file's header comment).

## Build & verify
```
pio run            # build firmware — the ONLY automated verification (no tests, no CI)
pio run -t upload  # flash via ST-LINK (openocd, custom upload_command in platformio.ini)
pio run -t monitor # serial monitor @ 115200 baud (ST-LINK VCP = USART3 PB10/PB11)
```

## Architecture
- **Display:** 800×480 DSI LCD (OTM8009A) via LTDC; LVGL software renderer draws directly into double-buffered SDRAM framebuffers (`-DLV_USE_DRAW_SW_ASM=0`; no DMA2D/Chrom-ART for LVGL despite SPECIFICATION.md §2 history)
- **Touch:** FT6206 on I2C1 (PB8/PB9, 400 kHz), polled by an LVGL pointer indev (`src/lvgl_display.c`); INT pin PJ5 unused (polling mode). If no controller is detected, `lvgl_display_init()` returns `TS_DEVICE_NOT_FOUND` and the UI still works via UART
- **Backlight / idle dim:** OTM8009A drives its own LED backlight via DSI reg 0x51 (`OTM8009A_CMD_WRDISBV`) — no MCU PWM pin. After ~5 s without touch, `src/lvgl_display.c` fades 0x7F→0x10 (~6%) over ~400 ms (30 ms LVGL timer, step 8); any touch ramps back up. Dimming is disabled when no TS controller is detected — nothing could wake the display
- **Framework:** STM32Cube HAL (`framework = stm32cube`; framework provides `STM32F469xx`, `USE_HAL_DRIVER`, `F_CPU=180000000L`)
- **UI:** LVGL 9.5 (registry dep `lvgl/lvgl@^9.1.0`), ARGB8888/32bpp, 8 MB SDRAM heap @0xC0300000. Single thermostat screen: hero temperature display (generated ~180 px digit font in `src/ui_font_temp.c`, superscript °F from `src/ui_font_unit.c`), setpoint chevron buttons, mode bar with HEAT/AC/AUTO (mutually exclusive) + FAN as an independent on/off toggle (active = colored fill, inactive = flat). Icons are generated A8 alpha-mask images in `include/ui_icons.h`, recolored per state. LVGL 9 event filters are exact matches — never OR multiple `LV_EVENT_*` codes in one `lv_obj_add_event_cb()` call
- **Comms:** UART3 @ 115200 baud on PB10/PB11 → ST-LINK VCP (debug out now; future host link per SPECIFICATION.md §5 — parser + device-side setters for `0x50`/`0x51`/`0x53`/`0x54` implemented, `0x52` GET_STATE and the host daemon not yet started). RX is polled 1 byte per loop iteration via non-blocking `HAL_UART_Receive()` in main.c; debug out is `dbg_print()` (blocking `HAL_UART_Transmit`)
- **State model:** `src/thermostat_state.c` — mode (HEAT/AC/AUTO) + independent fan toggle + current temp + setpoint. Current temp is fed from the thermistor at ~1 Hz; the old demo drift sits behind `THERMO_DEMO_SIM_ENABLED` (default 0, flip to re-enable for testing without a sensor). Onboard LEDs mirror the active mode (+ orange when fan on) for debug; `thermo_update()` rewrites those pins every loop, so don't drive them from elsewhere
- **Sensors:** thermistor on PB1 (board connector C8 "A0", net ARD_A0) → ADC1_IN9; single-shot read in `src/adc_thermistor.c`, converted to °F via the B-parameter equation (pure integer math — no FPU in this build), fed into current temp at ~1 Hz from main.c. Divider constants are ASSUMED 10 kΩ NTC @25 °C / B=3950 / 10 kΩ pull-up to VCC — confirm against the actual part. The F469's ADC is clocked by the SAI PLL — see Critical constraints
- **BSP:** LCD/SDRAM/TS drivers are vendored in `lib/stm32_bsp` (local lib in `lib_deps`, not the ST registry package). I2C1 for touch is lazily initialized inside the BSP (`TS_IO_Init()` → `I2C1_Init()`); no `MX_I2C1_Init` needed in `main.c`

Full spec: `SPECIFICATION.md` — read before generating UI screens, packet parsers, or state machines.

## Source layout
```
src/main.c              — HAL init, UART3 (PB10/PB11), boot checkpoints (LED_ORANGE toggled per stage), main superloop; SysTick_Handler feeds lv_tick_inc()
src/thermostat_state.c  — Thermostat state model + demo simulation, non-blocking UART parser, dbg_print() → HAL_UART_Transmit(USART3) is the debug path
src/lvgl_display.c      — LVGL port: direct-mode LTDC display, flush/swap protocol (LTDC_IRQHandler), FT6206 touch indev
src/adc_thermistor.c    — Thermistor ADC1_IN9 (PB1 / C8 "A0") + B-parameter °F conversion; pre-configures the SAI PLL Q divider for the ADC clock
src/ui_main_screen.c    — Thermostat screen: hero temp label, setpoint chevrons, mode bar; restyles on state change
src/ui_font_temp.c      — GENERATED ~180 px digit font (lv_font_conv); do not hand-edit — regenerate with the exact command saved in each file's header comment (source TTF: .pio/libdeps/disco_f469ni/lvgl/scripts/built_in_font/Montserrat-Medium.ttf)
src/ui_font_unit.c      — GENERATED ~108 px "°F" unit font (lv_font_conv); do not hand-edit
include/adc_thermistor.h  — Thermistor ADC init/read API
include/thermostat_state.h — Types, command IDs, packet framing constants, public API
include/ui_icons.h      — GENERATED icon images (tools/gen_icons.py); do not hand-edit
lib/stm32_bsp/          — Vendored ST BSP: stm32469i_discovery_{lcd,sdram,ts}.c + Components/ft6x06 driver
lv_conf.h               — LVGL config; SDRAM layout (FB1 @0xC0000000, FB2 @0xC0180000, 8 MB heap @0xC0300000)
tools/gen_icons.py      — Regenerates include/ui_icons.h (needs Pillow: pip install pillow)
tools/capture_screen.py — Converts an openocd SDRAM framebuffer dump into mockup-ui.jpg/.png
```

## Critical constraints (agents miss these)
- **`-DHSE_VALUE=8000000UL` is required** (in `platformio.ini`). This board's HSE crystal is 8 MHz; the framework hardcodes 25 MHz, which stretches every HAL tick/timeout ×3.125 in wall time and makes `HAL_DSI_Init` write an out-of-spec DSI PHY bit period. Removing it silently regresses boot timing (`SystemClock_Config()` uses PLLM=8 for non-REVA).
- **`LV_USE_FONT_COMPRESSED 1` is required** (in `lv_conf.h`). lv_font_conv emits RLE-compressed glyph data (`bitmap_format = LV_FONT_FMT_TXT_COMPRESSED`); without the flag every generated-font glyph decodes to NULL and labels render blank.
- **Boot order matters:** init UART3 right after `SystemClock_Config()` (BRR is computed from PCLK1) but *before* SDRAM/LCD init, so all boot phases are observable; set `DSI_WCR_LTDCEN` before `BSP_LCD_InitEx()` — `HAL_DSI_DeInit/Init` do not clear WCR, which is why it survives the BSP's internal re-init.
- **ADC clock = SAI PLL (PLLSAI_Q):** the F469's ADC has no selectable clock source — it is fed by the same SAI PLL that clocks the LTDC. `adc_thermistor_init()` must run *before* `BSP_LCD_InitEx()`: it pre-sets N=384/Q=12/R=7 (Q → 32 MHz for the ADC), and the BSP's internal `HAL_RCCEx_PeriphCLKConfig()` rewrites only N/R, preserving P/Q. If it runs after LCD init, the ADC is left with Q=0 (/2 = 192 MHz) and conversions time out — and enabling the PLL late would also change the LTDC clock mid-boot.
- **`-DDATA_IN_ExtSDRAM` is required** (in `platformio.ini`). It compiles out the SDRAM init inside `BSP_LCD_InitEx()`; main.c calls `BSP_SDRAM_Init()` (plus a retention self-test) explicitly before the LCD comes up. Without it SDRAM is initialized twice, inside LCD init, after the ADC PLL setup.
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
