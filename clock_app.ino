// NTP Digital Clock — ESP32-P4, JD9165 1024×600, GT911 touch, Ethernet
// No LVGL — direct PSRAM framebuffer, 7-segment blue-tech style.
// Touch: tap 12H / 24H buttons at bottom; tap − / + in status bar for brightness.
#include <Arduino.h>
#include "esp_sleep.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "driver/i2c_master.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <time.h>

#include "src/esp_lcd_jd9165.h"
#include "src/esp_lcd_touch_gt911.h"
#include "src/eth_driver.h"
#include "src/ui.h"

// ---- Pin / peripheral constants ---------------------------------------------
#define BSP_LCD_RST         GPIO_NUM_5
#define BSP_BACKLIGHT_GPIO  23
#define MIPI_PHY_LDO_CHAN   3
#define MIPI_PHY_LDO_MV     2500
#define LCD_W               1024
#define LCD_V               600
#define TOUCH_SDA           GPIO_NUM_7
#define TOUCH_SCL           GPIO_NUM_8
#define TOUCH_X_MAX         1024
#define TOUCH_Y_MAX         600

#define TIMEZONE_STR        "ICT-7"
#define NTP_SERVER          "pool.ntp.org"
#define NTP_SYNC_TIMEOUT_S  15
#define LOOP_DELAY_MS       500   // ~2 fps

// Touch debounce
#define DEBOUNCE_MODE_MS    400   // mode buttons
#define DEBOUNCE_BRT_MS     150   // brightness buttons (allow repeated taps)

// Brightness
#define BRT_MIN     1
#define BRT_MAX     10
#define BRT_DEFAULT 8

// Touch zone coordinates (mirror of ui.cpp layout)
#define BTN_Y       510
#define BTN_H       85
#define BTN_12H_X1  100
#define BTN_12H_X2  450
#define BTN_24H_X1  574
#define BTN_24H_X2  924
#define SBAR_H      46
#define BRT_MINUS_X1 680
#define BRT_MINUS_X2 720
#define BRT_PLUS_X1  934
#define BRT_PLUS_X2  974

// ---- LP GPIO fix — must run before initArduino() ---------------------------
__attribute__((constructor(200))) static void early_lp_periph_on() {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}

// ---- Globals ----------------------------------------------------------------
static esp_lcd_panel_handle_t s_panel     = NULL;
static esp_lcd_touch_handle_t s_tp        = NULL;
static uint16_t              *s_fb        = NULL;
static uint8_t                s_mode_12h  = 0;          // 0=24h, 1=12h
static uint8_t                s_brightness = BRT_DEFAULT; // 1-10
static volatile bool          s_ntp_synced = false;
static unsigned long          s_last_mode_ms = 0;
static unsigned long          s_last_brt_ms  = 0;

// ---- NVS helpers ------------------------------------------------------------
static void nvs_init_clock() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

static void save_u8(const char *key, uint8_t val) {
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, val);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint8_t load_u8(const char *key, uint8_t def) {
    nvs_handle_t h;
    uint8_t val = def;
    if (nvs_open("clock", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &val);
        nvs_close(h);
    }
    return val;
}

// ---- Backlight PWM ----------------------------------------------------------
static void set_backlight(uint8_t level) {
    // level 1-10 → PWM 102-1023
    uint32_t pwm = (uint32_t)level * 1023 / BRT_MAX;
    ledcWrite(BSP_BACKLIGHT_GPIO, pwm);
}

// ---- Display init -----------------------------------------------------------
static bool init_display() {
    esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = MIPI_PHY_LDO_CHAN,
        .voltage_mv = MIPI_PHY_LDO_MV,
    };
    if (esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan) != ESP_OK) {
        Serial.println("[LCD] LDO failed");
        return false;
    }

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    if (esp_lcd_new_dsi_bus(&bus_cfg, &mipi_dsi_bus) != ESP_OK) {
        Serial.println("[LCD] DSI bus failed");
        return false;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_cfg, &io));

    esp_lcd_dpi_panel_config_t dpi_cfg =
        JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.num_fbs = 1;

    jd9165_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus    = mipi_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = BSP_LCD_RST,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };
    if (esp_lcd_new_panel_jd9165(io, &dev_cfg, &s_panel) != ESP_OK) return false;
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    ledcAttach(BSP_BACKLIGHT_GPIO, 5000, 10);
    ledcWrite(BSP_BACKLIGHT_GPIO, 1023);  // full bright during splash

    size_t fb_bytes = LCD_W * LCD_V * sizeof(uint16_t);
    s_fb = (uint16_t *)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    if (!s_fb) { Serial.println("[LCD] PSRAM alloc failed"); return false; }

    Serial.printf("[LCD] OK, fb=%u bytes PSRAM\n", (unsigned)fb_bytes);
    return true;
}

// ---- Touch init -------------------------------------------------------------
static bool init_touch() {
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port                = I2C_NUM_1;
    bus_cfg.sda_io_num              = TOUCH_SDA;
    bus_cfg.scl_io_num              = TOUCH_SCL;
    bus_cfg.clk_source              = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt       = 7;
    bus_cfg.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK) {
        Serial.println("[TOUCH] I2C bus failed");
        return false;
    }

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 100000;
    if (esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io) != ESP_OK) {
        Serial.println("[TOUCH] panel IO failed");
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max        = TOUCH_X_MAX;
    tp_cfg.y_max        = TOUCH_Y_MAX;
    tp_cfg.rst_gpio_num = GPIO_NUM_NC;
    tp_cfg.int_gpio_num = GPIO_NUM_NC;

    if (esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_tp) != ESP_OK) {
        Serial.println("[TOUCH] GT911 init failed");
        s_tp = NULL;
        return false;
    }
    Serial.println("[TOUCH] GT911 OK");
    return true;
}

// ---- SNTP -------------------------------------------------------------------
static void init_sntp() {
    setenv("TZ", TIMEZONE_STR, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_init();
    Serial.println("[NTP] SNTP started");
}

static bool push_frame() {
    if (!s_panel || !s_fb) return false;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_W, LCD_V, s_fb);
    return true;
}

// ---- setup() ----------------------------------------------------------------
void setup() {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    delay(1500);
    Serial.println("=== clock_app starting ===");

    nvs_init_clock();
    s_mode_12h  = load_u8("mode", 0);
    s_brightness = load_u8("brt", BRT_DEFAULT);
    if (s_brightness < BRT_MIN) s_brightness = BRT_MIN;
    if (s_brightness > BRT_MAX) s_brightness = BRT_MAX;

    // Display
    if (!init_display()) { Serial.println("[FATAL] display init"); return; }

    // Apply loaded brightness (splash was at full bright)
    set_backlight(s_brightness);

    // Splash
    ui_draw_splash(s_fb, "Connecting...");
    push_frame();

    // Touch
    init_touch();

    // Ethernet
    eth_driver_init();

    Serial.println("[ETH] waiting for DHCP...");
    for (int i = 0; i < 60 && !eth_driver_got_ip(); i++) {
        delay(1000);
        if (i % 5 == 4) Serial.printf("[ETH] ...%ds\n", i + 1);
    }

    if (!eth_driver_got_ip()) {
        Serial.println("[ETH] DHCP timeout");
        ui_draw_splash(s_fb, "No Ethernet!");
        push_frame();
        delay(2000);
    } else {
        Serial.printf("[ETH] IP: %s\n", eth_driver_ip_str());
        init_sntp();

        ui_draw_splash(s_fb, "Syncing NTP...");
        push_frame();
        for (int i = 0; i < NTP_SYNC_TIMEOUT_S; i++) {
            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                s_ntp_synced = true;
                Serial.println("[NTP] synced OK");
                break;
            }
            delay(1000);
        }
        if (!s_ntp_synced) Serial.println("[NTP] timeout — using local clock");
    }
}

// ---- loop() -----------------------------------------------------------------
void loop() {
    if (!s_fb || !s_panel) { delay(5000); return; }

    // --- Touch handling ---
    if (s_tp) {
        esp_lcd_touch_read_data(s_tp);
        uint16_t tx[1], ty[1];
        uint8_t cnt = 0;
        if (esp_lcd_touch_get_coordinates(s_tp, tx, ty, NULL, &cnt, 1) && cnt > 0) {
            unsigned long now = millis();
            int px = (int)tx[0], py = (int)ty[0];

            // Mode buttons (bottom area)
            if (py >= BTN_Y && py < BTN_Y + BTN_H) {
                if (now - s_last_mode_ms > DEBOUNCE_MODE_MS) {
                    if (px >= BTN_12H_X1 && px <= BTN_12H_X2 && s_mode_12h != 1) {
                        s_mode_12h = 1;
                        save_u8("mode", s_mode_12h);
                        Serial.println("[TOUCH] → 12H");
                        s_last_mode_ms = now;
                    } else if (px >= BTN_24H_X1 && px <= BTN_24H_X2 && s_mode_12h != 0) {
                        s_mode_12h = 0;
                        save_u8("mode", s_mode_12h);
                        Serial.println("[TOUCH] → 24H");
                        s_last_mode_ms = now;
                    }
                }
            }

            // Brightness buttons (status bar top area)
            if (py < SBAR_H) {
                if (now - s_last_brt_ms > DEBOUNCE_BRT_MS) {
                    if (px >= BRT_MINUS_X1 && px <= BRT_MINUS_X2 && s_brightness > BRT_MIN) {
                        s_brightness--;
                        set_backlight(s_brightness);
                        save_u8("brt", s_brightness);
                        Serial.printf("[TOUCH] brightness → %d\n", s_brightness);
                        s_last_brt_ms = now;
                    } else if (px >= BRT_PLUS_X1 && px <= BRT_PLUS_X2 && s_brightness < BRT_MAX) {
                        s_brightness++;
                        set_backlight(s_brightness);
                        save_u8("brt", s_brightness);
                        Serial.printf("[TOUCH] brightness → %d\n", s_brightness);
                        s_last_brt_ms = now;
                    }
                }
            }
        }
    }

    // NTP sync check (ongoing after boot)
    if (!s_ntp_synced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_ntp_synced = true;
        Serial.println("[NTP] synced");
    }

    // Get local time and render
    struct tm timeinfo;
    time_t now_t = time(NULL);
    localtime_r(&now_t, &timeinfo);

    ui_draw_frame(s_fb, &timeinfo, s_mode_12h, s_ntp_synced,
                  eth_driver_ip_str(), s_brightness);
    push_frame();

    delay(LOOP_DELAY_MS);
}
