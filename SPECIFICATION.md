# SYSTEM SPECIFICATION: DESK COMMANDER & TELEMETRY HUB
# Target Hardware: STM32F469I-DISCO
# UI Framework: LVGL (v8.x or v9.x)
# Author Engine: AI Coding Agent Prompt Spec

## 1. SYSTEM ARCHITECTURE OVERVIEW
The system operates as an asynchronous, single-core embedded workspace controller. 
The STM32 manages the high-resolution capacitive touch display, local state machine, 
and UI animations using hardware acceleration (Chrom-ART / DMA2D). 

Because the hardware lacks onboard Wi-Fi, network payloads are processed by a local 
host (Python Daemon/Home Server) and bridged to the STM32 via a lightweight transport layer.

Use code with caution.+------------------+         GitLab Webhooks / Teams Events|  Python Daemon   | <====== (Web Extension / Graph API)+--------+---------+|| [UART / Serial @ 115200 baud]| OR [Ethernet Raw Socket / lwIP]v+---------------------------------------+| STM32 DEV BOARD                       ||   +-------------------------------+   ||   | LVGL UI ENGINE                |   ||   | - Tab 0: Dashboard (PC Stats) |   ||   | - Tab 1: Focus (Timer)        |   ||   | - Tab 2: Lighting Control     |   ||   | - Tab 3: Git / Notifications  |   ||   +---------------+---------------+   ||                   |                   ||                   | [GPIO PWM / SPI]  ||                   v                   ||         [WS2812B LED Light Strip]     |+---------------------------------------+
## 2. FIRMWARE & HARDWARE CONSTRAINTS (STM32)
When generating code for this system, adhere to the following hardware limits:
* **Display Configuration:** 800x480 resolution, 16-bit or 24-bit color depth (RGB565 / RGB888 depending on SDRAM configuration).
* **Acceleration:** Leverage STM32 Chrom-ART (DMA2D) for LVGL rendering pipelines to keep CPU overhead under 15%.
* **Memory Constraints:** UI assets (icons, custom fonts) must fit within external SDRAM allocation boundaries. Frame buffers must utilize double-buffering via DMA.
* **Peripherals:** 
  * UART / USART for inbound PC metrics and notifications.
  * Timers configured for microsecond ticks to feed `lv_tick_inc()`.
  * PWM / SPI data line allocated for WS2812B LED strip (or serial passthrough to an ESP/WLED module).

## 3. LVGL UI LAYER ARCHITECTURE (800x480 Canvas)
The UI uses a **Left Navigation Bar** variant layout. A fixed menu container handles screen routing, bypassing the stock `lv_tabview` swipe animations to optimize render performance.

### 3.1 Global Navigation Container (Left Anchor)
* **Dimensions:** Width: 100px, Height: 480px.
* **Widgets:** Vertical flex container containing 5 flat image-buttons:
  1. `btn_nav_dash`: Dashboard Icon (Default active)
  2. `btn_nav_focus`: Clock / Timer Icon
  3. `btn_nav_light`: Bulb Icon
  4. `btn_nav_notify`: Bell Icon (with absolute-positioned overlay red dot badge for unseen states)
  5. `btn_nav_gitlab`: GitLab Logo

### 3.2 Display Screen Configurations
* **Screen 1: Dashboard (Telemetry)**
  * Three `lv_arc` elements scaled as half-circles mapping CPU Load (%), GPU Temp (°C), and RAM Usage (GB).
  * One `lv_chart` (Type: Line) mapping live download/upload network bandwidth streams.
* **Screen 2: Focus (Pomodoro)**
  * Centered large `lv_label` showing countdown time (`MM:SS`).
  * Concentric `lv_arc` indicating progress toward focus completion.
  * Start/Pause/Reset controller buttons.
* **Screen 3: Lighting Panel**
  * One `lv_colorwheel` for custom shelf color casting.
  * Linear `lv_slider` targeting global LED brightness adjustments.
* **Screen 4: Notifications Log**
  * Vertical scrolling `lv_list` populated dynamically via string manipulation queues.
* **Screen 5: GitLab Overview**
  * Kanban-style display grids highlighting open Merge Requests and repository stability tickers.

## 4. LIGHTWEIGHT SERIAL COMMUNICATION PROTOCOL
To avoid heavy JSON processing on the STM32, the intermediate Python runtime sanitizes all webhook and scraping feeds into deterministic byte streams.

### 4.1 Packet Framing Spec
Packets use a 1-byte header, 1-byte length argument, variable payload array, and an optional simple checksum byte.
`[START_BYTE: 0x02] [COMMAND_ID: 1 Byte] [DATA_LEN: 1 Byte] [DATA_BYTES] [END_BYTE: 0x03]`

### 4.2 Telemetry Command IDs (Frequency: 1Hz - 2Hz)
* **`0x10` (CPU Metric):** Data byte 1 = Load (0-100), Data byte 2 = Temp (°C)
* **`0x11` (GPU Metric):** Data byte 1 = Load (0-100), Data byte 2 = Temp (°C)
* **`0x12` (Network Metric):** Data bytes 1-4 = Net Speed encoded as float or fixed integer payload.

### 4.3 Workplace Automation Toggles
* **`0x20` (Microsoft Teams Status Broadcast):**
  * Payload `0x01` (Available) -> Set Nav Status LED Indicator Green.
  * Payload `0x02` (Busy/In-Meeting) -> Set Nav Status LED Indicator Red. Trigger local Desk Light warning modes.
  * Payload `0x03` (Away) -> Dim UI Backlight, shift desk environment to idle arrays.
* **`0x30` (GitLab Merge Request Alert):**
  * Payload contains a character string string mapping the name of the branch or MR text.
  * Action: Pause local task handling loop, parse text to an absolute notification popup widget overlaying current screens, pulse LEDs green.

## 5. REACTION & EFFECT ARCHITECTURE (STATE ENGINE)
The system moves through localized states dictated by asynchronous incoming events.
stateDiagram-v2
    [*] --> STATE_IDLE
    
    STATE_IDLE --> STATE_MEETING : Packet 0x20 (In Meeting)
    STATE_MEETING --> STATE_IDLE : Packet 0x20 (Available)
    
    STATE_IDLE --> STATE_CELEBRATE : Packet 0x30 (MR Merged)
    STATE_MEETING --> STATE_CELEBRATE : Packet 0x30 (MR Merged)
    
    state STATE_CELEBRATE {
        [*] --> RunParticleAnimation
        RunParticleAnimation --> PulseLEDsGreen
    }
    
    STATE_CELEBRATE --> STATE_IDLE : Timer Exceeded (3 Seconds) / Restore Previous State


### 5.1 LED Behavior Tables
* **`STATE_IDLE`:** Solid subtle white backdrop lighting or slow-moving pastel rainbow gradient loop.
* **`STATE_MEETING`:** Solid block scarlet/crimson lighting at 100% luminance. Block dynamic patterns.
* **`STATE_CELEBRATE`:** High-frequency, green and white chasing pattern across individual addressable pixel sections.

## 6. CODE CODE IMPLEMENTATION REQUIREMENTS FOR AI VISUAL AGENTS
When writing C or Python automation blocks supporting this structure, follow these syntax constraints:
1. **No Blocking Code:** Do not use `HAL_Delay()` anywhere in the main operational loops. Frame processing and UART sampling must happen strictly via non-blocking interrupts, timers (`HAL_GetTick()`), or FreeRTOS tasks.
2. **Object Memory Bounds:** Use static memory or explicit LVGL object pooling structures for notification logs to avoid dynamic heap fragmentation (`malloc` runaways) inside embedded loops.
3. **Python Independence:** The Chrome Web Extension or Graph API worker running on the computer must send data directly via local loopback web requests (`localhost:port`). It should not require authentication updates inside the raw embedded architecture directly.