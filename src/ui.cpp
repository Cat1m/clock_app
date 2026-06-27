// 7-segment digital clock UI — direct RGB565 framebuffer, no LVGL.
// Screen 1024×600. Blue-tech LED style: black bg, cyan-blue glowing segments.
#include "ui.h"
#include "font.h"
#include <string.h>
#include <stdio.h>

#define LCD_W   1024
#define LCD_H   600

// ---- Color palette (RGB565) --------------------------------------------------
#define COL_BG          0x0000u   // pure black
#define COL_STATUS_BG   0x0841u   // very dark blue-gray (status bar)
#define COL_SEG_ON      0x05FFu   // bright sky-blue  (R=0  G=191 B=248)
#define COL_SEG_OFF     0x00A5u   // dim ghost-blue   (R=0  G=20  B=40) — barely visible
#define COL_DATE        0x3DFFu   // light blue       (R=56 G=188 B=248)
#define COL_NTP_OK      0x07E0u   // bright green
#define COL_NTP_WAIT    0xFFE0u   // bright yellow
#define COL_STATUS_TXT  0x7BFFu   // pale blue-white (status bar text)
#define COL_AMPM        0xFD20u   // warm amber (AM/PM label)
#define COL_SEP         0x0421u   // dim separator line
#define COL_BTN_ACT_BG  0x051Fu   // solid blue fill  (active button)
#define COL_BTN_ACT_FG  0xFFFFu   // white            (active button text)
#define COL_BTN_INA_BG  0x0000u   // black            (inactive button)
#define COL_BTN_INA_BD  0x02B3u   // dim steel-blue border
#define COL_BTN_INA_FG  0x02B3u   // dim steel-blue text
#define COL_BRT_ON      0x05FFu   // filled brightness block
#define COL_BRT_OFF     0x0208u   // empty brightness block

// ---- Layout constants --------------------------------------------------------

// Status bar
#define SBAR_H      46    // status bar height (y=0..45)

// 7-segment digit bounding box
#define DIGIT_W     140
#define DIGIT_H     287
#define SEG_T       17    // segment thickness
#define SEG_INS     10    // inset from corners (gap between adjacent segments)

// Time block: HH:MM:SS starting at (TIME_X, TIME_Y)
#define TIME_Y      65
#define TIME_X      15

// X positions of each digit (computed from TIME_X=15, gap=10, colon half-width=19)
#define X_HH_T  (15)           // HH tens
#define X_HH_U  (165)          // HH units
#define COL1_CX (336)          // colon 1 centre-x
#define X_MM_T  (367)          // MM tens
#define X_MM_U  (517)          // MM units
#define COL2_CX (688)          // colon 2 centre-x
#define X_SS_T  (719)          // SS tens
#define X_SS_U  (869)          // SS units

// Colon dot positions (squares, 24×24 px)
#define COLON_SZ    24
#define COLON_DOT1_Y  (TIME_Y + DIGIT_H / 3  - COLON_SZ / 2)
#define COLON_DOT2_Y  (TIME_Y + DIGIT_H * 2 / 3 - COLON_SZ / 2)

// Date label
#define DATE_Y  (TIME_Y + DIGIT_H + 24)   // ~376

// Buttons
#define BTN_Y       510
#define BTN_H       85
#define BTN_12H_X1  100
#define BTN_12H_X2  450
#define BTN_24H_X1  574
#define BTN_24H_X2  924

// Brightness control in status bar (touch targets exported via ui.h)
#define BRT_MINUS_X1  680
#define BRT_MINUS_X2  720
#define BRT_PLUS_X1   934
#define BRT_PLUS_X2   974

// ---- Segment encoding --------------------------------------------------------
// bit 0=a(top) 1=b(top-right) 2=c(bot-right) 3=d(bottom)
// bit 4=e(bot-left) 5=f(top-left) 6=g(middle)
static const uint8_t SEG_CODE[10] = {
    0x3F,  // 0: a b c d e f
    0x06,  // 1: b c
    0x5B,  // 2: a b d e g
    0x4F,  // 3: a b c d g
    0x66,  // 4: b c f g
    0x6D,  // 5: a c d f g
    0x7D,  // 6: a c d e f g
    0x07,  // 7: a b c
    0x7F,  // 8: a b c d e f g
    0x6F,  // 9: a b c d f g
};

// ---- Primitives --------------------------------------------------------------

static inline void set_pixel(uint16_t *fb, int x, int y, uint16_t col) {
    if ((unsigned)x < LCD_W && (unsigned)y < LCD_H)
        fb[(unsigned)y * LCD_W + (unsigned)x] = col;
}

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t col) {
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if ((unsigned)yy >= LCD_H) continue;
        uint16_t *line = fb + (unsigned)yy * LCD_W;
        for (int c = 0; c < w; c++) {
            int xx = x + c;
            if ((unsigned)xx < LCD_W) line[xx] = col;
        }
    }
}

static void draw_rect_outline(uint16_t *fb, int x, int y, int w, int h,
                               int thick, uint16_t col) {
    fill_rect(fb, x,           y,           w,     thick, col);  // top
    fill_rect(fb, x,           y + h - thick, w,   thick, col);  // bottom
    fill_rect(fb, x,           y,           thick, h,     col);  // left
    fill_rect(fb, x + w - thick, y,         thick, h,     col);  // right
}

// ---- 7-segment digit ---------------------------------------------------------

static void draw_digit(uint16_t *fb, int x, int y, int digit) {
    uint8_t code = (digit >= 0 && digit <= 9) ? SEG_CODE[digit] : 0;

    const uint16_t on[7] = {
        (code & 0x01) ? COL_SEG_ON : COL_SEG_OFF,  // a top
        (code & 0x02) ? COL_SEG_ON : COL_SEG_OFF,  // b top-right
        (code & 0x04) ? COL_SEG_ON : COL_SEG_OFF,  // c bot-right
        (code & 0x08) ? COL_SEG_ON : COL_SEG_OFF,  // d bottom
        (code & 0x10) ? COL_SEG_ON : COL_SEG_OFF,  // e bot-left
        (code & 0x20) ? COL_SEG_ON : COL_SEG_OFF,  // f top-left
        (code & 0x40) ? COL_SEG_ON : COL_SEG_OFF,  // g middle
    };

    int W = DIGIT_W, H = DIGIT_H, T = SEG_T, INS = SEG_INS;
    int hH = H / 2;  // half height

    // a — top horizontal
    fill_rect(fb, x + INS,    y,            W - 2*INS, T,           on[0]);
    // b — top-right vertical
    fill_rect(fb, x + W - T,  y + INS,      T,         hH - 2*INS,  on[1]);
    // c — bottom-right vertical
    fill_rect(fb, x + W - T,  y + hH + INS, T,         hH - 2*INS,  on[2]);
    // d — bottom horizontal
    fill_rect(fb, x + INS,    y + H - T,    W - 2*INS, T,           on[3]);
    // e — bottom-left vertical
    fill_rect(fb, x,          y + hH + INS, T,         hH - 2*INS,  on[4]);
    // f — top-left vertical
    fill_rect(fb, x,          y + INS,      T,         hH - 2*INS,  on[5]);
    // g — middle horizontal
    fill_rect(fb, x + INS,    y + hH - T/2, W - 2*INS, T,           on[6]);
}

// ---- Colon (two square dots) -------------------------------------------------

static void draw_colon(uint16_t *fb, int cx, bool visible) {
    uint16_t col = visible ? COL_SEG_ON : COL_SEG_OFF;
    int half = COLON_SZ / 2;
    fill_rect(fb, cx - half, COLON_DOT1_Y, COLON_SZ, COLON_SZ, col);
    fill_rect(fb, cx - half, COLON_DOT2_Y, COLON_SZ, COLON_SZ, col);
}

// ---- Status bar (top 46 px) -------------------------------------------------

static const char *MONTH[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char *WDAY[7] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};

static void draw_status_bar(uint16_t *fb, bool ntp_synced,
                             const char *ip_str, uint8_t brightness) {
    // Background
    for (int y = 0; y < SBAR_H; y++)
        for (int x = 0; x < LCD_W; x++)
            fb[y * LCD_W + x] = COL_STATUS_BG;

    // Bottom separator line
    for (int x = 0; x < LCD_W; x++)
        fb[SBAR_H * LCD_W + x] = COL_SEP;

    // NTP status
    uint16_t col_ntp = ntp_synced ? COL_NTP_OK : COL_NTP_WAIT;
    render_text(fb, 10, 15, ntp_synced ? "NTP OK" : "NTP...", 1, col_ntp);

    // IP address
    if (ip_str && ip_str[0]) {
        char buf[28];
        snprintf(buf, sizeof(buf), "IP:%s", ip_str);
        render_text(fb, 175, 15, buf, 1, COL_STATUS_TXT);
    }

    // Brightness "−" button
    draw_rect_outline(fb, BRT_MINUS_X1, 8, 40, 30, 2, COL_STATUS_TXT);
    render_char(fb, BRT_MINUS_X1 + 16, 15, '-', 1, COL_STATUS_TXT);

    // Brightness level blocks (10 blocks, each 18×20 px, 2 px gap)
    for (int i = 0; i < 10; i++) {
        int bx = 728 + i * 20;
        fill_rect(fb, bx, 13, 18, 20,
                  (uint8_t)(i + 1) <= brightness ? COL_BRT_ON : COL_BRT_OFF);
    }

    // Brightness "+" button
    draw_rect_outline(fb, BRT_PLUS_X1, 8, 40, 30, 2, COL_STATUS_TXT);
    render_char(fb, BRT_PLUS_X1 + 16, 15, '+', 1, COL_STATUS_TXT);
}

// ---- Date label -------------------------------------------------------------

static void draw_date(uint16_t *fb, const struct tm *t) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s  %02d  %s  %04d",
             WDAY[t->tm_wday], t->tm_mday,
             MONTH[t->tm_mon], t->tm_year + 1900);
    int tw = text_width(buf, 2);
    render_text(fb, (LCD_W - tw) / 2, DATE_Y, buf, 2, COL_DATE);
}

// ---- AM/PM label (12H mode only, centred below date) ------------------------

static void draw_ampm(uint16_t *fb, const struct tm *t) {
    const char *label = (t->tm_hour < 12) ? "AM" : "PM";
    int scale = 3;
    int tw = text_width(label, scale);
    // Place below date label, above buttons
    int y = DATE_Y + FONT_H * 2 + 18;   // DATE_Y + date height(32) + gap
    render_text(fb, (LCD_W - tw) / 2, y, label, scale, COL_AMPM);
}

// ---- Mode buttons (bottom strip) --------------------------------------------

static void draw_one_button(uint16_t *fb, int x1, int y, int w, int h,
                             const char *label, bool active) {
    uint16_t bg = active ? COL_BTN_ACT_BG : COL_BTN_INA_BG;
    uint16_t fg = active ? COL_BTN_ACT_FG : COL_BTN_INA_FG;
    uint16_t bd = active ? COL_BTN_ACT_BG : COL_BTN_INA_BD;

    fill_rect(fb, x1, y, w, h, bg);
    draw_rect_outline(fb, x1, y, w, h, 3, bd);

    int tw = text_width(label, 3);
    int th = FONT_H * 3;
    render_text(fb, x1 + (w - tw) / 2, y + (h - th) / 2, label, 3, fg);
}

static void draw_buttons(uint16_t *fb, uint8_t mode_12h) {
    // Thin separator line above button area
    for (int x = 0; x < LCD_W; x++)
        fb[505 * LCD_W + x] = COL_SEP;

    draw_one_button(fb, BTN_12H_X1, BTN_Y, BTN_12H_X2 - BTN_12H_X1, BTN_H,
                    "12H", mode_12h != 0);
    draw_one_button(fb, BTN_24H_X1, BTN_Y, BTN_24H_X2 - BTN_24H_X1, BTN_H,
                    "24H", mode_12h == 0);
}

// ---- Public API -------------------------------------------------------------

void ui_draw_frame(uint16_t *fb, const struct tm *t,
                   uint8_t mode_12h, bool ntp_synced,
                   const char *ip_str, uint8_t brightness) {
    // Clear to black
    memset(fb, 0, LCD_W * LCD_H * sizeof(uint16_t));

    // Status bar
    draw_status_bar(fb, ntp_synced, ip_str, brightness);

    // --- Compute display hours ---
    int hour = t->tm_hour;
    if (mode_12h) {
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }

    // --- Draw six 7-segment digits ---
    draw_digit(fb, X_HH_T, TIME_Y, hour / 10);
    draw_digit(fb, X_HH_U, TIME_Y, hour % 10);
    draw_digit(fb, X_MM_T, TIME_Y, t->tm_min / 10);
    draw_digit(fb, X_MM_U, TIME_Y, t->tm_min % 10);
    draw_digit(fb, X_SS_T, TIME_Y, t->tm_sec / 10);
    draw_digit(fb, X_SS_U, TIME_Y, t->tm_sec % 10);

    // Colons — blink: on when seconds is even
    bool col_on = (t->tm_sec % 2 == 0);
    draw_colon(fb, COL1_CX, col_on);
    draw_colon(fb, COL2_CX, col_on);

    // Date label
    draw_date(fb, t);

    // AM/PM label (12H mode only)
    if (mode_12h) draw_ampm(fb, t);

    // Mode buttons
    draw_buttons(fb, mode_12h);
}

void ui_draw_splash(uint16_t *fb, const char *msg) {
    memset(fb, 0, LCD_W * LCD_H * sizeof(uint16_t));
    int tw = text_width(msg, 3);
    render_text(fb, (LCD_W - tw) / 2,
                    LCD_H / 2 - FONT_H * 3 / 2,
                    msg, 3, COL_SEG_ON);
}
