/*
 * LVGL clock UI — ESP32-P4 1024×600
 * Dark blue-tech theme: cyan time, steel-blue date, amber AM/PM.
 */
#include "ui_lvgl.h"
#include "ui_fonts.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* ---- Widget handles --------------------------------------------------------- */
static lv_obj_t *s_lbl_ntp   = NULL;
static lv_obj_t *s_lbl_ip    = NULL;
static lv_obj_t *s_lbl_brt   = NULL;
static lv_obj_t *s_btn_brt_m = NULL;
static lv_obj_t *s_btn_brt_p = NULL;
static lv_obj_t *s_lbl_time  = NULL;
static lv_obj_t *s_lbl_date  = NULL;
static lv_obj_t *s_lbl_ampm  = NULL;
static lv_obj_t *s_btn_12h   = NULL;
static lv_obj_t *s_lbl_12h   = NULL;
static lv_obj_t *s_btn_24h   = NULL;
static lv_obj_t *s_lbl_24h   = NULL;

/* ---- State ------------------------------------------------------------------ */
static uint8_t      s_brt_val  = 8;
static uint8_t      s_mode_val = 0;
static ui_mode_cb_t s_on_mode  = NULL;
static ui_brt_cb_t  s_on_brt   = NULL;

/* ---- Internal helpers ------------------------------------------------------- */

static void update_brt_label(void) {
    char buf[12];
    snprintf(buf, sizeof(buf), "BRT %d", s_brt_val);
    lv_label_set_text(s_lbl_brt, buf);
}

static void apply_mode_buttons(void) {
    bool act12 = (s_mode_val == 1);

    lv_obj_set_style_bg_color(s_btn_12h,
        lv_color_hex(act12 ? 0x0040C0 : 0x050D1A), 0);
    lv_obj_set_style_border_color(s_btn_12h,
        lv_color_hex(act12 ? 0x4080FF : 0x204060), 0);
    lv_obj_set_style_text_color(s_lbl_12h,
        act12 ? lv_color_white() : lv_color_hex(0x407090), 0);

    lv_obj_set_style_bg_color(s_btn_24h,
        lv_color_hex(!act12 ? 0x0040C0 : 0x050D1A), 0);
    lv_obj_set_style_border_color(s_btn_24h,
        lv_color_hex(!act12 ? 0x4080FF : 0x204060), 0);
    lv_obj_set_style_text_color(s_lbl_24h,
        !act12 ? lv_color_white() : lv_color_hex(0x407090), 0);

    if (s_lbl_ampm) {
        if (act12) lv_obj_clear_flag(s_lbl_ampm, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(s_lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Button event handlers -------------------------------------------------- */

static void on_btn_12h(lv_event_t *e) {
    if (s_mode_val == 1) return;
    s_mode_val = 1;
    apply_mode_buttons();
    if (s_on_mode) s_on_mode(1);
}

static void on_btn_24h(lv_event_t *e) {
    if (s_mode_val == 0) return;
    s_mode_val = 0;
    apply_mode_buttons();
    if (s_on_mode) s_on_mode(0);
}

static void on_btn_brt_m(lv_event_t *e) {
    if (s_brt_val <= 1) return;
    s_brt_val--;
    update_brt_label();
    if (s_on_brt) s_on_brt(s_brt_val);
}

static void on_btn_brt_p(lv_event_t *e) {
    if (s_brt_val >= 10) return;
    s_brt_val++;
    update_brt_label();
    if (s_on_brt) s_on_brt(s_brt_val);
}

/* ---- Small button factory (for status bar) ---------------------------------- */

static lv_obj_t *make_sbar_btn(lv_obj_t *parent, const char *txt,
                                 lv_event_cb_t cb) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 48, 38);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F2030), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A3850), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x204060), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xA0C8E0), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl);
    return btn;
}

/* ---- Public: init ----------------------------------------------------------- */

void ui_lvgl_init(ui_mode_cb_t on_mode, ui_brt_cb_t on_brt,
                  uint8_t init_mode, uint8_t init_brt) {
    s_on_mode  = on_mode;
    s_on_brt   = on_brt;
    s_mode_val = init_mode;
    s_brt_val  = init_brt;

    /* ── Screen ─────────────────────────────────────────────────────── */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000810), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── Status bar ──────────────────────────────────────────────────── */
    lv_obj_t *sbar = lv_obj_create(scr);
    lv_obj_set_size(sbar, 1024, 55);
    lv_obj_set_pos(sbar, 0, 0);
    lv_obj_set_style_bg_color(sbar, lv_color_hex(0x050D1A), 0);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sbar, 0, 0);
    lv_obj_set_style_radius(sbar, 0, 0);
    lv_obj_set_style_pad_all(sbar, 0, 0);
    lv_obj_set_scrollbar_mode(sbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom separator line on status bar */
    lv_obj_set_style_border_side(sbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(sbar, lv_color_hex(0x0C2040), 0);
    lv_obj_set_style_border_width(sbar, 1, 0);

    /* NTP label */
    s_lbl_ntp = lv_label_create(sbar);
    lv_label_set_text(s_lbl_ntp, "NTP...");
    lv_obj_set_style_text_font(s_lbl_ntp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_ntp, lv_color_hex(0xFFD000), 0);
    lv_obj_align(s_lbl_ntp, LV_ALIGN_LEFT_MID, 10, 0);

    /* IP label */
    s_lbl_ip = lv_label_create(sbar);
    lv_label_set_text(s_lbl_ip, "---");
    lv_obj_set_style_text_font(s_lbl_ip, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(0x607090), 0);
    lv_obj_align(s_lbl_ip, LV_ALIGN_LEFT_MID, 115, 0);

    /* Brightness [+] button — rightmost */
    s_btn_brt_p = make_sbar_btn(sbar, "+", on_btn_brt_p);
    lv_obj_align(s_btn_brt_p, LV_ALIGN_RIGHT_MID, -10, 0);

    /* BRT N label — left of [+] */
    s_lbl_brt = lv_label_create(sbar);
    lv_obj_set_style_text_font(s_lbl_brt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_brt, lv_color_hex(0x00BFDF), 0);
    lv_obj_align_to(s_lbl_brt, s_btn_brt_p, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    update_brt_label();

    /* Brightness [-] button — left of label */
    s_btn_brt_m = make_sbar_btn(sbar, "-", on_btn_brt_m);
    lv_obj_align_to(s_btn_brt_m, s_lbl_brt, LV_ALIGN_OUT_LEFT_MID, -12, 0);

    /* ── Time label — custom font 128pt, native (no scale) ──────────── */
    s_lbl_time = lv_label_create(scr);
    lv_label_set_text(s_lbl_time, "--:--:--");
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_clock_128, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_text_letter_space(s_lbl_time, 4, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -68);

    /* ── Date label — custom font 48pt, native (no scale) ───────────── */
    s_lbl_date = lv_label_create(scr);
    lv_label_set_text(s_lbl_date, "--- -- --- ----");
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_date_48, 0);
    lv_obj_set_style_text_color(s_lbl_date, lv_color_hex(0x4E9FE0), 0);
    lv_obj_set_style_text_letter_space(s_lbl_date, 2, 0);
    lv_obj_align(s_lbl_date, LV_ALIGN_CENTER, 0, +80);

    /* ── AM/PM label (12H mode only) — 36pt, inline after date ──────── */
    s_lbl_ampm = lv_label_create(scr);
    lv_label_set_text(s_lbl_ampm, "AM");
    lv_obj_set_style_text_font(s_lbl_ampm, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_ampm, lv_color_hex(0xFFA040), 0);
    lv_obj_align(s_lbl_ampm, LV_ALIGN_CENTER, 0, +148);
    lv_obj_add_flag(s_lbl_ampm, LV_OBJ_FLAG_HIDDEN);  /* shown when 12H active */

    /* ── Bottom button bar ───────────────────────────────────────────── */
    lv_obj_t *btnbar = lv_obj_create(scr);
    lv_obj_set_size(btnbar, 1024, 90);
    lv_obj_set_pos(btnbar, 0, 510);
    lv_obj_set_style_bg_color(btnbar, lv_color_hex(0x050D1A), 0);
    lv_obj_set_style_bg_opa(btnbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btnbar, 0, 0);
    lv_obj_set_style_border_side(btnbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(btnbar, lv_color_hex(0x0C2040), 0);
    lv_obj_set_style_border_width(btnbar, 1, 0);
    lv_obj_set_style_radius(btnbar, 0, 0);
    lv_obj_set_style_pad_all(btnbar, 0, 0);
    lv_obj_set_scrollbar_mode(btnbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(btnbar, LV_OBJ_FLAG_SCROLLABLE);

    /* 12H button */
    s_btn_12h = lv_button_create(btnbar);
    lv_obj_set_size(s_btn_12h, 340, 64);
    lv_obj_set_pos(s_btn_12h, 100, 13);
    lv_obj_set_style_radius(s_btn_12h, 8, 0);
    lv_obj_set_style_border_width(s_btn_12h, 2, 0);
    lv_obj_set_style_shadow_width(s_btn_12h, 0, 0);
    lv_obj_add_event_cb(s_btn_12h, on_btn_12h, LV_EVENT_CLICKED, NULL);

    s_lbl_12h = lv_label_create(s_btn_12h);
    lv_label_set_text(s_lbl_12h, "12H");
    lv_obj_set_style_text_font(s_lbl_12h, &lv_font_montserrat_24, 0);
    lv_obj_center(s_lbl_12h);

    /* 24H button */
    s_btn_24h = lv_button_create(btnbar);
    lv_obj_set_size(s_btn_24h, 340, 64);
    lv_obj_set_pos(s_btn_24h, 584, 13);
    lv_obj_set_style_radius(s_btn_24h, 8, 0);
    lv_obj_set_style_border_width(s_btn_24h, 2, 0);
    lv_obj_set_style_shadow_width(s_btn_24h, 0, 0);
    lv_obj_add_event_cb(s_btn_24h, on_btn_24h, LV_EVENT_CLICKED, NULL);

    s_lbl_24h = lv_label_create(s_btn_24h);
    lv_label_set_text(s_lbl_24h, "24H");
    lv_obj_set_style_text_font(s_lbl_24h, &lv_font_montserrat_24, 0);
    lv_obj_center(s_lbl_24h);

    /* Apply initial styles */
    apply_mode_buttons();
}

/* ---- Public: update --------------------------------------------------------- */

static const char *const WDAY[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *const MON[12] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

void ui_lvgl_update(const struct tm *t, uint8_t mode_12h,
                    bool ntp_synced, const char *ip, uint8_t brightness) {
    if (!s_lbl_time) return;
    if (!lvgl_port_lock(50)) return;

    /* Sync external state if changed */
    if (brightness != s_brt_val) {
        s_brt_val = brightness;
        update_brt_label();
    }
    if (mode_12h != s_mode_val) {
        s_mode_val = mode_12h;
        apply_mode_buttons();
    }

    /* NTP status */
    lv_obj_set_style_text_color(s_lbl_ntp,
        ntp_synced ? lv_color_hex(0x00E040) : lv_color_hex(0xFFD000), 0);
    lv_label_set_text(s_lbl_ntp, ntp_synced ? "NTP OK" : "NTP...");

    /* IP */
    if (ip && ip[0]) {
        lv_label_set_text(s_lbl_ip, ip);
    } else {
        lv_label_set_text(s_lbl_ip, "---");
    }

    /* Time — colon blinks on even seconds */
    int hour = t->tm_hour;
    if (mode_12h) {
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }
    char tbuf[16];
    bool colon = (t->tm_sec % 2 == 0);
    snprintf(tbuf, sizeof(tbuf), colon ? "%02d:%02d:%02d" : "%02d %02d %02d",
             hour, t->tm_min, t->tm_sec);
    lv_label_set_text(s_lbl_time, tbuf);

    /* Date */
    char dbuf[32];
    snprintf(dbuf, sizeof(dbuf), "%s  %02d  %s  %04d",
             WDAY[t->tm_wday], t->tm_mday,
             MON[t->tm_mon], t->tm_year + 1900);
    lv_label_set_text(s_lbl_date, dbuf);

    /* AM/PM */
    if (s_lbl_ampm) {
        lv_label_set_text(s_lbl_ampm, t->tm_hour < 12 ? "AM" : "PM");
    }

    lvgl_port_unlock();
}
