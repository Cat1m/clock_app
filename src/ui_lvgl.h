#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called from clock_app.ino when mode or brightness changes via LVGL button. */
typedef void (*ui_mode_cb_t)(uint8_t new_mode_12h);
typedef void (*ui_brt_cb_t)(uint8_t new_brightness);

/**
 * Create all LVGL widgets.  Must be called under lvgl_port_lock().
 *
 * @param on_mode    callback invoked when 12H/24H button is tapped
 * @param on_brt     callback invoked when brightness ±1 is tapped
 * @param init_mode  initial 12H/24H mode (0=24H, 1=12H)
 * @param init_brt   initial brightness level (1–10)
 */
void ui_lvgl_init(ui_mode_cb_t on_mode, ui_brt_cb_t on_brt,
                  uint8_t init_mode, uint8_t init_brt);

/**
 * Update all dynamic labels.  Acquires/releases lvgl_port_lock() internally.
 * Safe to call from Arduino loop().
 */
void ui_lvgl_update(const struct tm *t, uint8_t mode_12h,
                    bool ntp_synced, const char *ip, uint8_t brightness);

#ifdef __cplusplus
}
#endif
