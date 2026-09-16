# Handoff: LVGL display + slow boot on STM32F469I-DISCO

Status as of 2026-09-16: **COMPLETE — verified on hardware.** All fixes implemented, flashed, and
confirmed working by the user: fast boot, correct clock prints, LVGL main screen with visible buttons.
This document is kept as a record of the root causes and fixes (see "What was done" below).

## Mission (original)

Fix two regressions introduced while adding LVGL buttons to the 800x480 DSI LCD:

1. **Slow boot**: seconds-to-minutes of total silence after power-on before `[DBG] System online!`.
2. **Black screen**: previously a solid red boot indicator was visible; with the LVGL changes
   nothing visible appeared (expected: dark UI with title + two buttons).

## What was done (this session)

### Repo state restored
The previous session's WIP had been stashed (`stash@{0}` on branch `test`, HEAD `876be06`).
It has been fully restored to the working tree (verified byte-identical, including untracked
LVGL files). The stash entry still exists — safe to drop once verification passes.

### Fixes implemented
1. **`platformio.ini`**: added `-DHSE_VALUE=8000000UL`. Board is revC with an 8 MHz HSE crystal;
   the framework hardcodes 25 MHz, which (a) stretched every SysTick-based delay/timeout ×3.125
   in wall time and (b) made `HAL_DSI_Init` write an out-of-spec DSI PHY bit period (UIX4=2
   instead of 8). All three framework definitions are `#ifndef`-guarded, so the flag overrides them.
2. **Global framework package** (`~/.platformio/packages/framework-stm32cubef4/.../Src/stm32f4xx_hal_dsi.c`):
   removed leftover non-stock `dbg_print("ok")`/`dbg_print("timeout")` calls inside
   `DSI_ShortWrite()` (a previous session's instrumentation; would break any project lacking a
   `dbg_print` symbol). NOTE: this file is outside the repo.
3. **`src/main.c`**: moved `MX_USART3_UART_Init()` to right after `SystemClock_Config()` so all
   boot phases are observable on UART (BRR stays correct — computed from true PCLK1=45 MHz);
   moved `hdsi_eval.Instance->WCR |= DSI_WCR_LTDCEN` to before `BSP_LCD_InitEx()` (DeInit/Init
   don't clear WCR, verified in HAL source); removed the now-redundant manual USART3 BRR override.
4. **`src/lvgl_display.c`**: added `lv_theme_default_init(disp, 0x4C8BF5, 0xF5A623, dark=true,
   &lv_font_montserrat_14)` + `lv_display_set_theme()` — without a theme, buttons have no
   background style and label text defaults to black (invisible on the dark UI).
   **`lv_conf.h`**: enabled `LV_FONT_MONTSERRAT_14` alongside 28.

### Docs updated
- `docs/Learnings.md`: full clock root cause + fix, framework-package incident, theme gotcha,
  NT35510-probe boot cost note.
- `AGENTS.md`: removed stale DMA2D/RGB565 claims; documented the `-DHSE_VALUE` requirement and
  boot-ordering constraint.

### Build
`pio run` → SUCCESS (full rebuild with new flags, ~4 s). Flash 13.6% used, RAM 0.8%.

## Hardware verification (completed 2026-09-16)

A hung `openocd` upload had left the ST-LINK USB connection stuck in the kernel; a physical
replug fixed it. Flash + UART capture then confirmed: fast boot, correct clock values, LCD status
0, and the LVGL main screen with visible buttons (user-confirmed).

### Steps that were used (kept for reference)
1. `lsusb | grep 0483` — confirm normal device number (not "Device 000").
2. Flash + capture boot UART in one go:
   ```bash
   nohup ~/.platformio/penv/bin/python /tmp/opencode/capture_uart.py > /tmp/opencode/boot_capture.log 2>&1 &
   sleep 3; pio run -t upload; echo "RESET_AT $(date +%s.%N)"
   # wait ~15 s, then inspect /tmp/opencode/boot_capture.log
   ```
   (`/tmp/opencode/capture_uart.py` reads /dev/ttyACM0 @115200 for 14 s and timestamps the first byte.)
3. **Expected results:**
   - First UART byte within ~1–2 s of reset (was: many seconds to minutes).
   - `SYSCLK = 180000000 Hz`, `HCLK = 180000000`, `PCLK1 = 45000000`, `PCLK2 = 90000000`.
   - `LCD Init Result Status: 0` (OTM8009A ID read OK).
   - `[DBG] LVGL UI online` followed by state-machine logs.
   - LCD: red boot indicator briefly → dark main screen with **visible** title + two gray buttons
     (not black/blank). ~2 s later the auto-demo pulses the orange LED 3×.
4. If boot is still multi-second: halt mid-silence via ST-LINK (`stm32-stlink` skill) and take a
   backtrace to find which DSI wait eats time; likely candidate is the NT35510 ID probe in
   `LCD_ReadType()` (one full ~1 s DSI read timeout per boot — see Learnings.md).

## Pitfalls / constraints

- Do NOT commit unless the user asks.
- Keep `-DHSE_VALUE=8000000UL` in build_flags permanently; removing it silently regresses boot
  timing and DSI PHY config.
- The `.pio/libdeps/disco_f469ni/stm32_bsp/` copy shadows `lib/stm32_bsp/` — delete it after any
  BSP edit (see Learnings.md).
- Board revision defaults to REVC; the `USE_STM32469I_DISCO_REVA` branches are for revA's
  25 MHz crystal — don't "simplify" them away.
- ST-LINK/openocd quirks: a stuck openocd can leave both the target and the USB connection in a
  bad state; `st-flash --reset read <f> 0x08000000 16` recovers the *target*, but a kernel-level
  USB hang needs a physical replug (observed 2026-09-16).
