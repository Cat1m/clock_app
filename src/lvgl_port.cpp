/*
 * LVGL v9 port — ESP32-P4 MIPI DSI (JD9165) + GT911 touch
 * Adapted from Espressif lvgl_port_v9 reference example.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#if SOC_LCDCAM_RGB_LCD_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif
#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
/* lvgl_private.h is only needed for triple-buffer (mode 2) internals */
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_BUFFER_NUMS == 3)
#include "lvgl_private.h"
#endif
#include "lvgl_port.h"

static const char *TAG = "lv_port";

typedef struct {
    esp_lcd_panel_handle_t lcd_handle;
    esp_lcd_touch_handle_t tp_handle;
    bool is_init;
} lvgl_port_task_param_t;

typedef esp_err_t (*get_lcd_frame_buffer_cb_t)(esp_lcd_panel_handle_t panel, uint32_t fb_num, void **fb0, ...);

static SemaphoreHandle_t lvgl_mux;
static TaskHandle_t      lvgl_task_handle   = NULL;
static lvgl_port_interface_t lvgl_port_interface = LVGL_PORT_INTERFACE_RGB;

#if LVGL_PORT_AVOID_TEAR_ENABLE
static get_lcd_frame_buffer_cb_t lvgl_get_lcd_frame_buffer = NULL;
#endif

/* ---- Flush callbacks -------------------------------------------------------- */

#if LVGL_PORT_AVOID_TEAR_ENABLE

static void switch_lcd_frame_buffer_to(esp_lcd_panel_handle_t panel_handle, void *fb)
{
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LVGL_PORT_H_RES, LVGL_PORT_V_RES, fb);
}

#if LVGL_PORT_DIRECT_MODE

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    if (lv_disp_flush_is_last(disp)) {
        switch_lcd_frame_buffer_to(panel_handle, color_map);
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_disp_flush_ready(disp);
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_BUFFER_NUMS == 2

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    switch_lcd_frame_buffer_to(panel_handle, color_map);
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    lv_disp_flush_ready(disp);
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_BUFFER_NUMS == 3

static void *lvgl_port_rgb_last_buf  = NULL;
static void *lvgl_port_rgb_next_buf  = NULL;
static void *lvgl_port_flush_next_buf = NULL;

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    if (disp->buf_act == disp->buf_1) {
        disp->buf_2->data = lvgl_port_flush_next_buf;
    } else {
        disp->buf_1->data = lvgl_port_flush_next_buf;
    }
    lvgl_port_flush_next_buf = color_map;
    switch_lcd_frame_buffer_to(panel_handle, color_map);
    lvgl_port_rgb_next_buf = color_map;
    lv_disp_flush_ready(disp);
}

#endif /* LVGL_PORT_DIRECT_MODE / LVGL_PORT_FULL_REFRESH */

#else  /* !LVGL_PORT_AVOID_TEAR_ENABLE */

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (lvgl_port_interface != LVGL_PORT_INTERFACE_MIPI_DSI_DMA) {
        lv_disp_flush_ready(disp);
    }
}

#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

/* ---- Display init ----------------------------------------------------------- */

static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);

    void *buf1 = NULL;
    void *buf2 = NULL;
    int buffer_size = 0;

    ESP_LOGD(TAG, "Malloc memory for LVGL buffer");
#if LVGL_PORT_AVOID_TEAR_ENABLE
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_BUFFER_NUMS == 3) && LVGL_PORT_FULL_REFRESH
    ESP_ERROR_CHECK(lvgl_get_lcd_frame_buffer(panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2));
    lvgl_port_rgb_next_buf   = lvgl_port_rgb_last_buf;
    lvgl_port_flush_next_buf = buf2;
#else
    ESP_ERROR_CHECK(lvgl_get_lcd_frame_buffer(panel_handle, 2, &buf1, &buf2));
#endif
#else
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    assert(buf1);
    ESP_LOGI(TAG, "LVGL buffer size: %dKB", buffer_size * (int)sizeof(lv_color_t) / 1024);
#endif

    ESP_LOGD(TAG, "Register display driver to LVGL");
    lv_display_t *display = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);

    lv_display_set_buffers(
        display, buf1, buf2, buffer_size * sizeof(lv_color_t),
#if LVGL_PORT_FULL_REFRESH
        LV_DISPLAY_RENDER_MODE_FULL
#elif LVGL_PORT_DIRECT_MODE
        LV_DISPLAY_RENDER_MODE_DIRECT
#else
        LV_DISPLAY_RENDER_MODE_PARTIAL
#endif
    );
    lv_display_set_flush_cb(display, flush_callback);
    lv_display_set_user_data(display, panel_handle);

    return display;
}

/* ---- Touch indev ------------------------------------------------------------ */

static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev_drv);
    assert(tp);

    uint16_t tx = 0, ty = 0;
    uint8_t  cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &tx, &ty, NULL, &cnt, 1);
    if (pressed && cnt > 0) {
        data->point.x = tx;
        data->point.y = ty;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_read_cb(indev, touchpad_read);

    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev, g);
    return indev;
}

/* ---- Tick timer ------------------------------------------------------------- */

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = &tick_increment,
        .name     = "LVGL tick"
    };
    esp_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    return esp_timer_start_periodic(timer, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

/* ---- LVGL task -------------------------------------------------------------- */

static void lvgl_port_task(void *arg)
{
    ESP_LOGD(TAG, "Starting LVGL task");

    lvgl_port_task_param_t *param = (lvgl_port_task_param_t *)arg;

    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_display_t *disp = display_init(param->lcd_handle);
    assert(disp);

    if (param->tp_handle) {
        lv_indev_t *indev = indev_init(param->tp_handle);
        assert(indev);
    }

    param->is_init = true;

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* ---- Public API ------------------------------------------------------------- */

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                          esp_lcd_touch_handle_t tp_handle,
                          lvgl_port_interface_t  interface)
{
    lvgl_port_task_param_t lvgl_task_param = {
        .lcd_handle = lcd_handle,
        .tp_handle  = tp_handle,
        .is_init    = false,
    };

    lvgl_port_interface = interface;

#if LVGL_PORT_AVOID_TEAR_ENABLE
    switch (interface) {
#if SOC_LCDCAM_RGB_LCD_SUPPORTED
    case LVGL_PORT_INTERFACE_RGB:
        lvgl_get_lcd_frame_buffer = (get_lcd_frame_buffer_cb_t)esp_lcd_rgb_panel_get_frame_buffer;
        break;
#endif
#if SOC_MIPI_DSI_SUPPORTED
    case LVGL_PORT_INTERFACE_MIPI_DSI_DMA:
    case LVGL_PORT_INTERFACE_MIPI_DSI_NO_DMA:
        lvgl_get_lcd_frame_buffer = (get_lcd_frame_buffer_cb_t)esp_lcd_dpi_panel_get_frame_buffer;
        break;
#endif
    default:
        ESP_LOGE(TAG, "Invalid interface type");
        return ESP_ERR_INVALID_ARG;
    }
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    ESP_LOGI(TAG, "Create LVGL task");
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : (BaseType_t)LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE,
        &lvgl_task_param, LVGL_PORT_TASK_PRIORITY,
        &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    while (!lvgl_task_param.is_init) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "LVGL port OK (mode=%d, fbs=%d)", LVGL_PORT_AVOID_TEAR_MODE, LVGL_PORT_LCD_BUFFER_NUMS);
    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_mux);
}

bool lvgl_port_notify_lcd_vsync(void)
{
    BaseType_t need_yield = pdFALSE;
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3)
    if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
        lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf;
        lvgl_port_rgb_last_buf   = lvgl_port_rgb_next_buf;
    }
#elif LVGL_PORT_AVOID_TEAR_ENABLE
    if (lvgl_task_handle) {
        xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield);
    }
#else
    if (lvgl_port_interface == LVGL_PORT_INTERFACE_MIPI_DSI_DMA) {
        lv_display_t *disp = lv_disp_get_default();
        lv_disp_flush_ready(disp);
    }
#endif
    return (need_yield == pdTRUE);
}
