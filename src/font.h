#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 8×16 bitmap font, ASCII 0x20–0x7E
// Each character = 16 bytes (1 byte per row, MSB = leftmost pixel)
#define FONT_W 8
#define FONT_H 16

extern const uint8_t FONT_8X16[95][16]; // index 0 = space (0x20)

// Render a single ASCII character at (x,y) with pixel scale and color
// fb: 1024×600 RGB565 framebuffer
void render_char(uint16_t *fb, int x, int y, char c, int scale, uint16_t color);

// Render null-terminated ASCII string
void render_text(uint16_t *fb, int x, int y, const char *str, int scale, uint16_t color);

// Helper: measure text width in pixels
int text_width(const char *str, int scale);

#ifdef __cplusplus
}
#endif
