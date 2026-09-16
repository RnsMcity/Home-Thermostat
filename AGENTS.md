# AGENTS.md — DESK COMMANDER / TELEMETRY HUB (STM32F469I-DISCO)

## Project type
PlatformIO embedded C for STM32F469NIH6 (Cortex-M4 @ 180 MHz, 2 MB Flash). Board in `platformio.ini` is `disco_f469ni`. External SDRAM @0xC0000000 — layout in `lv_conf.h`.

## Build & verify
```
pio run            # build firmware
pio run -t upload  # flash via ST-LINK (openocd)
pio run -t monitor # serial monitor @ 115200 baud
```

## Architecture
- **Display:** 800×480 DSI LCD (OTM8009A) via LTDC; LVGL software renderer draws directly into double-buffered SDRAM framebuffers (`-DLV_USE_DRAW_SW_ASM=0`; no DMA2D/Chrom-ART for LVGL despite SPECIFICATION.md §2)
- **Touch:** FT6206 on I2C1 (PB8/PB9, 400 kHz), polled by an LVGL pointer indev (`src/lvgl_display.c`); INT pin PJ5 unused (polling mode). If no controller is detected, `lvgl_display_init()` returns `TS_DEVICE_NOT_FOUND` and the UI still works via UART `CMD_UI_PRESS_BTN`
- **Framework:** STM32Cube HAL (`framework = stm32cube`; framework provides `STM32F469xx`, `USE_HAL_DRIVER`, `F_CPU=180000000L`)
- **UI:** LVGL 9.5 (registry dep `lvgl/lvgl@^9.1.0`), ARGB8888/32bpp, 8 MB SDRAM heap @0xC0300000
- **Comms:** UART3 @ 115200 baud on PB10/PB11 → ST-LINK VCP (loopback to host Python daemon)
- **LEDs:** WS2812B strip — `set_shelf_leds_raw()` is a stub, not yet wired to SPI DMA or TIM PWM
- **BSP:** LCD/SDRAM/TS drivers are vendored in `lib/stm32_bsp` (local lib in `lib_deps`, not the ST registry package). I2C1 for touch is lazily initialized inside the BSP (`TS_IO_Init()` → `I2C1_Init()`); no `MX_I2C1_Init` needed in `main.c`

Full spec: `SPECIFICATION.md` — read before generating UI screens, packet parsers, or state machines.

## Source layout
```
src/main.c              — HAL init, UART3 (PB10/PB11), boot checkpoints (LED_ORANGE toggled per stage), main superloop; SysTick_Handler feeds lv_tick_inc()
src/workspace_states.c  — State machine, telemetry handlers, non-blocking UART parser, notification ring queue; dbg_print() → HAL_UART_Transmit(USART3) is the debug path
src/lvgl_display.c      — LVGL port: direct-mode LTDC display, flush/swap protocol (LTDC_IRQHandler), FT6206 touch indev
src/ui_main_screen.c    — Main screen: title + "Pulse LEDs" button + state-display button, auto-demo press ~2 s after boot
include/workspace_states.h — Types, command IDs (incl. CMD_UI_PRESS_BTN 0x40), packet framing constants, public API
lib/stm32_bsp/          — Vendored ST BSP: stm32469i_discovery_{lcd,sdram,ts}.c + Components/ft6x06 driver
lv_conf.h               — LVGL config; SDRAM layout (FB1 @0xC0000000, FB2 @0xC0180000, 8 MB heap @0xC0300000)
```
`include/debug_uart.h` is dead code — it declares functions that don't exist and lists wrong pins (PD8/PD9); nothing includes it. Use `dbg_print()`.

## Critical constraints (agents miss these)
- **`-DHSE_VALUE=8000000UL` is required** (in `platformio.ini`). This board's HSE crystal is 8 MHz; the framework hardcodes 25 MHz, which stretches every HAL tick/timeout ×3.125 in wall time and makes `HAL_DSI_Init` write an out-of-spec DSI PHY bit period. Removing it silently regresses boot timing (`SystemClock_Config()` uses PLLM=8 for non-REVA).
- **Boot order matters:** init UART3 right after `SystemClock_Config()` (BRR is computed from PCLK1) but *before* SDRAM/LCD init, so all boot phases are observable; set `DSI_WCR_LTDCEN` before `BSP_LCD_InitEx()` — `HAL_DSI_DeInit/Init` do not clear WCR, which is why it survives the BSP's internal re-init.
- **Never use `HAL_Delay()`** in main loops (boot phase may). Use `HAL_GetTick()`, non-blocking interrupts, or timer callbacks.
- **No `malloc` inside embedded loops.** Static allocation only — notification queue is a fixed ring buffer (`NOTIF_QUEUE_MAX=16`).
- **Frame buffers live in external SDRAM** (two full 800×480×4B buffers); the active one is swapped on LTDC refresh — see `src/lvgl_display.c`. Never assume they fit in internal RAM.
- **LVGL tick:** fed from `SysTick_Handler()` (main.c), not the main loop.
- **UART packet format:** `[0x02] [CMD_ID: 1B] [LEN: 1B] [DATA...] [END: 0x03]`. Command IDs in `include/workspace_states.h` and `SPECIFICATION.md` §4. The parser dispatches on data length and never validates the END byte.
- **State machine auto-cycles** every 5s for demo (`DEMO_CYCLE_INTERVAL_MS`). Real operation is event-driven via UART commands.
- **Onboard LED clobbering:** `state_machine_update()` rewrites all four onboard LED pins every loop iteration. UI code that drives the LEDs must call `ui_set_led_override(ms)` first (see `on_btn_pulse` in `src/ui_main_screen.c`).

## Stub functions (need real implementations)
- `set_shelf_leds_raw()` — TODO: WS2812B driver via SPI DMA or TIM PWM
- `trigger_lvgl_ui_refresh()` — no-op; called from the notification drain loop, needs a real LVGL refresh path

## Onboard LEDs (for demo/debug, not the shelf strip)
| LED | Pin    | Color  |
|-----|--------|--------|
| LD1 | PG6    | Green  |
| LD2 | PD4    | Orange |
| LD3 | PD5    | Red    |
| LD4 | PK3    | Blue   |

## VS Code / IDE
- Extension: `platformio.platformio-ide` (not cpptools-extension-pack)
- `.vscode/c_cpp_properties.json` is auto-generated by PlatformIO — do not hand-edit it.
