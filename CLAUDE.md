# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-P4 IoT Digital Clock with a 1024×600 touch LCD, Ethernet, NTP time sync, and LVGL v9 UI. Written in Arduino C++ with custom C hardware drivers.

## Build & Flash

This project targets Arduino IDE (or PlatformIO) with ESP32-P4 board support.

**Build in Arduino IDE:**
1. Select board: `ESP32P4 Dev Module`
2. Sketch → Verify/Compile (`Ctrl+R`)
3. Sketch → Upload (`Ctrl+U`) to flash

**Partition table:** `partitions.csv` is referenced by the build system — NVS at 20 KB, factory app at 16.4 MB, coredump at 64 KB.

There is no automated test suite; verification is done by flashing and observing hardware behavior.

## Architecture

The code is organized in three layers:

### 1. Hardware Drivers (`src/`)

| File | Purpose |
|------|---------|
| `esp_lcd_jd9165.{c,h}` | JD9165 MIPI-DSI LCD controller — 1024×600 @ 60 Hz, RGB565, 2-lane @ 750 Mbps |
| `esp_lcd_touch.{c,h}` | Abstract touch interface (up to 5 points) |
| `esp_lcd_touch_gt911.{c,h}` | GT911 capacitive touch driver — I2C @ 0x5D, 100 kHz |
| `eth_driver.{cpp,h}` | Ethernet RMII driver — DHCP, generic 802.3 PHY |

### 2. Middleware (`src/`)

| File | Purpose |
|------|---------|
| `lvgl_port.{cpp,h}` | LVGL v9 port — FreeRTOS task (priority 4, 16 KB stack), vsync ISR, mutex-guarded rendering, PSRAM-backed buffers |
| `lv_mem_psram.cpp` | Custom LVGL allocator routing all heap to PSRAM, freeing ~128 KB internal SRAM |

### 3. Application

| File | Purpose |
|------|---------|
| `clock_app.ino` | Main sketch — hardware init, NVS read/write, main loop (500 ms tick) |
| `src/ui_lvgl.{cpp,h}` | LVGL UI: large cyan time (128pt custom font), date (48pt custom font), AM/PM, 12H/24H toggle, brightness ±1 buttons |
| `src/ui_fonts.h` | `LV_FONT_DECLARE` cho hai custom font: `lv_font_clock_128`, `lv_font_date_48` |
| `src/lv_font_clock_128.c` | Custom font — Montserrat 128pt, Bpp=4, chars `0123456789: APM` |
| `src/lv_font_date_48.c` | Custom font — Montserrat 48pt, Bpp=4, full day/month charset |
| `lv_conf.h` | LVGL config — 128 KB PSRAM heap, RGB565, avoid-tear mode 3, 2 LCD buffers + direct mode |

### Data Flow

```
Ethernet DHCP → IP acquired
    → SNTP sync (pool.ntp.org, ICT-7 / UTC+7)
        → main loop reads localtime() every 500 ms
            → ui_lvgl updates time/date labels
            → colon blinks on even seconds
```

## lv_conf.h — Các setting đã chuẩn hóa

| Setting | Giá trị | Lý do |
|---|---|---|
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CUSTOM` | LVGL heap → PSRAM qua `src/lv_mem_psram.cpp` |
| `LV_COLOR_DEPTH` | `16` | RGB565, 2 bytes/pixel |
| `LV_USE_OS` | `LV_OS_NONE` | `lvgl_port.cpp` tự quản lý lock/unlock |
| `LV_DEF_REFR_PERIOD` | `100` ms | Clock chỉ cần ~2fps; 10fps là đủ, tiết kiệm CPU |
| `LV_DRAW_SW_DRAW_UNIT_CNT` | `1` | Phải giữ `1` khi `LV_USE_OS=LV_OS_NONE` — LVGL báo `#error` nếu >1 mà không có OS mutex |
| `LV_THEME_DEFAULT_DARK` | `1` | Dark theme |

**Widgets đã tắt** (không dùng trong clock app): `CALENDAR`, `CHART`, `KEYBOARD`, `TEXTAREA`, `SPINBOX`, `BUILD_EXAMPLES`.

**Lưu ý v9:** Custom allocator trong v9 dùng hàm C (`lv_malloc_core`, `lv_realloc_core`, `lv_free_core`) trong `src/lv_mem_psram.cpp`, **không phải macro** như v8. Thiếu file này sẽ gây linker error.

## Key Configuration

**`clock_app.ino` constants:**
- `TIMEZONE_STR "ICT-7"` — UTC+7 (Bangkok/Hanoi)
- `NTP_SERVER "pool.ntp.org"`
- `BRT_MIN/MAX/DEFAULT` — brightness 1–10, default 8
- `LCD_W 1024`, `LCD_H 600`

**NVS namespace `"clock"`:**
- `"mode"` — `0` = 24H, `1` = 12H
- `"brt"` — brightness level 1–10

**Hardware pins:**
- LCD Reset: GPIO 5
- Backlight PWM: GPIO 23 (5 kHz, 10-bit)
- Touch I2C: SDA=GPIO 7, SCL=GPIO 8
- Ethernet RMII: GPIOs 28–35, 49–52
- MIPI PHY LDO: channel 3, 2.5 V

## LVGL Threading Model

LVGL runs exclusively in a FreeRTOS task created by `lvgl_port.cpp`. Any code outside that task (including `loop()`) must acquire the LVGL mutex before calling any `lv_*` API:

```cpp
lvgl_port_lock(0);   // 0 = wait indefinitely
lv_label_set_text(label, text);
lvgl_port_unlock();
```

The vsync ISR calls `lv_display_flush_ready()` to signal frame completion — do not flush manually.

## Custom Fonts

Hai font được generate từ `https://lvgl.io/tools/fontconverter` với Montserrat TTF (Google Fonts):

| File | Name | Size | Bpp | Symbols |
|---|---|---|---|---|
| `src/lv_font_clock_128.c` | `lv_font_clock_128` | 128pt | 4 | `0123456789: APM` |
| `src/lv_font_date_48.c` | `lv_font_date_48` | 48pt | 4 | `A-Z a-z 0-9 space ,.` |

**Tái tạo font:** Upload `Montserrat-Regular.ttf`, điền Name/Size/Bpp/Symbols như bảng trên, Output format = `C file`, download `.c` → copy vào `src/` ghi đè.

**Không dùng `lv_obj_set_style_transform_scale`** — nearest-neighbor scaling gây pixel vỡ ở scale cao. Native font vector luôn sharp.

## Design Decisions Worth Knowing

- **PSRAM for LVGL**: Internal SRAM is scarce; all LVGL objects go to PSRAM via the custom allocator in `lv_mem_psram.cpp`. Changing `lv_conf.h` memory settings requires matching changes there.
- **Avoid-tear mode 3**: Direct DMA mode with 2 frame buffers. Switching to single-buffer or non-direct mode changes flush semantics in `lvgl_port.cpp`.
- **Generic 802.3 PHY**: No named PHY driver — uses `esp_eth_phy_new_generic_802_3` with OUI `0x0121C6`, addr 1. PHY-specific registers are not touched.
- **NTP soft-fail**: The sketch waits up to 15 s for sync, then continues with unsynchronized local time. The UI shows NTP status in the status bar.
