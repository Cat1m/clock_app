// NTP Digital Clock — ESP32-P4, JD9165 1024×600, GT911 touch, Ethernet
// UI: LVGL v9, dark blue-tech theme, cyan time, 12H/24H buttons, brightness ±.
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
#include "src/lvgl_port.h"
#include "src/ui_lvgl.h"

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
#define LOOP_DELAY_MS       500

#define BRT_MIN     1
#define BRT_MAX     10
#define BRT_DEFAULT 8

// ---- LP GPIO fix — must run before initArduino() ---------------------------
__attribute__((constructor(200))) static void early_lp_periph_on() {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}

// ---- Globals ----------------------------------------------------------------
static esp_lcd_panel_handle_t s_panel     = NULL;
static esp_lcd_touch_handle_t s_tp        = NULL;
static uint8_t                s_mode_12h  = 0;
static uint8_t                s_brightness = BRT_DEFAULT;
static volatile bool          s_ntp_synced = false;

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
    uint32_t pwm = (uint32_t)level * 1023 / BRT_MAX;
    ledcWrite(BSP_BACKLIGHT_GPIO, pwm);
}

// ---- LVGL UI callbacks (called from LVGL task context) ----------------------
static void on_mode_change(uint8_t m) {
    s_mode_12h = m;
    save_u8("mode", m);
    Serial.printf("[UI] mode → %dH\n", m ? 12 : 24);
}

static void on_brt_change(uint8_t b) {
    s_brightness = b;
    set_backlight(b);
    save_u8("brt", b);
    Serial.printf("[UI] brightness → %d\n", b);
}

// ---- Vsync ISR callback — notifies LVGL task --------------------------------
static IRAM_ATTR bool on_refresh_done(esp_lcd_panel_handle_t panel,
                                       esp_lcd_dpi_panel_event_data_t *edata,
                                       void *user_ctx) {
    return lvgl_port_notify_lcd_vsync();
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
    dpi_cfg.num_fbs = LVGL_PORT_LCD_BUFFER_NUMS;

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

    esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = on_refresh_done };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL));

    ledcAttach(BSP_BACKLIGHT_GPIO, 5000, 10);
    ledcWrite(BSP_BACKLIGHT_GPIO, 0);  // start dark until LVGL renders first frame

    Serial.println("[LCD] panel OK");
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

// ---- UI helper: update display with current system state --------------------
static void update_ui_now() {
    time_t now_t = time(NULL);
    struct tm t;
    localtime_r(&now_t, &t);
    ui_lvgl_update(&t, s_mode_12h, s_ntp_synced, eth_driver_ip_str(), s_brightness);
}

// ---- setup() ----------------------------------------------------------------
void setup() {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    delay(1500);
    Serial.begin(115200);
    Serial.println("=== clock_app starting ===");

    nvs_init_clock();
    s_mode_12h   = load_u8("mode", 0);
    s_brightness = load_u8("brt", BRT_DEFAULT);
    if (s_brightness < BRT_MIN) s_brightness = BRT_MIN;
    if (s_brightness > BRT_MAX) s_brightness = BRT_MAX;

    if (!init_display()) { Serial.println("[FATAL] display init"); return; }

    init_touch();

    ESP_ERROR_CHECK(lvgl_port_init(s_panel, s_tp, LVGL_PORT_INTERFACE_MIPI_DSI_DMA));

    if (lvgl_port_lock(-1)) {
        ui_lvgl_init(on_mode_change, on_brt_change, s_mode_12h, s_brightness);
        lvgl_port_unlock();
    }

    /* Turn backlight on after first LVGL frame is ready */
    vTaskDelay(pdMS_TO_TICKS(100));
    set_backlight(s_brightness);

    eth_driver_init();

    Serial.println("[ETH] waiting for DHCP...");
    for (int i = 0; i < 60 && !eth_driver_got_ip(); i++) {
        delay(1000);
        update_ui_now();
        if (i % 5 == 4) Serial.printf("[ETH] ...%ds\n", i + 1);
    }

    if (!eth_driver_got_ip()) {
        Serial.println("[ETH] DHCP timeout — no NTP");
    } else {
        Serial.printf("[ETH] IP: %s\n", eth_driver_ip_str());
        init_sntp();

        for (int i = 0; i < NTP_SYNC_TIMEOUT_S; i++) {
            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                s_ntp_synced = true;
                Serial.println("[NTP] synced OK");
                break;
            }
            delay(1000);
            update_ui_now();
        }
        if (!s_ntp_synced) Serial.println("[NTP] timeout — using local clock");
    }
}

// ---- loop() -----------------------------------------------------------------
void loop() {
    if (!s_ntp_synced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_ntp_synced = true;
        Serial.println("[NTP] synced");
    }
    update_ui_now();
    delay(LOOP_DELAY_MS);
}
