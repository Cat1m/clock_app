#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Draw 7-segment digital clock frame into RGB565 framebuffer (1024×600).
// t          : broken-down local time from localtime_r()
// mode_12h   : 1 = 12-hour display, 0 = 24-hour
// ntp_synced : controls status bar color/label
// ip_str     : Ethernet IP string, shown in status bar
// brightness : 0–9 brightness level (shown in status bar)
void ui_draw_frame(uint16_t *fb,
                   const struct tm *t,
                   uint8_t mode_12h,
                   bool ntp_synced,
                   const char *ip_str,
                   uint8_t brightness);

// Touch zone definitions — check these in clock_app.ino
// Mode buttons:    y > 510, x 100..450 = 12H,  x 574..924 = 24H
// Brightness:      y < 46,  x 680..720 = dim,  x 936..976 = bright

// Splash screen shown during boot / waiting for DHCP
void ui_draw_splash(uint16_t *fb, const char *msg);

#ifdef __cplusplus
}
#endif
