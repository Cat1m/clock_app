/*
 * LVGL v9 port — ESP32-P4 MIPI DSI (JD9165) + GT911 touch
 * Adapted from Espressif lvgl_port_v9 reference example.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------- Inline replacements for pins_config.h ----------- */
#define EXAMPLE_LVGL_PORT_TASK_MAX_DELAY_MS   500
#define EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS   5
#define EXAMPLE_LVGL_PORT_TASK_PRIORITY       4
#define EXAMPLE_LVGL_PORT_TASK_STACK_SIZE_KB  32
#define EXAMPLE_LVGL_PORT_TASK_CORE           (-1)
#define EXAMPLE_LVGL_PORT_TICK                2     /* ms per LVGL tick */
#define EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE   1
#define EXAMPLE_LVGL_PORT_AVOID_TEAR_MODE     3     /* mode 3: 2 LCD buf + direct */
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE_    0
#define EXAMPLE_LVGL_PORT_PPA_ROTATION_ENABLE 0
/* ------------------------------------------------------------- */

typedef enum {
    LVGL_PORT_INTERFACE_RGB,
    LVGL_PORT_INTERFACE_MIPI_DSI_DMA,
    LVGL_PORT_INTERFACE_MIPI_DSI_NO_DMA,
    LVGL_PORT_INTERFACE_MAX,
} lvgl_port_interface_t;

/* Display resolution */
#define LVGL_PORT_H_RES             (1024)
#define LVGL_PORT_V_RES             (600)

/* Tick period */
#define LVGL_PORT_TICK_PERIOD_MS    (EXAMPLE_LVGL_PORT_TICK)

/* Task parameters */
#define LVGL_PORT_TASK_MAX_DELAY_MS (EXAMPLE_LVGL_PORT_TASK_MAX_DELAY_MS)
#define LVGL_PORT_TASK_MIN_DELAY_MS (EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS)
#define LVGL_PORT_TASK_STACK_SIZE   (EXAMPLE_LVGL_PORT_TASK_STACK_SIZE_KB * 1024)
#define LVGL_PORT_TASK_PRIORITY     (EXAMPLE_LVGL_PORT_TASK_PRIORITY)
#define LVGL_PORT_TASK_CORE         (EXAMPLE_LVGL_PORT_TASK_CORE)

/* Buffer params (used only when avoid-tear is disabled — kept for completeness) */
#define LVGL_PORT_BUFFER_MALLOC_CAPS  (MALLOC_CAP_SPIRAM)
#define LVGL_PORT_BUFFER_HEIGHT       (100)

/* Avoid-tear derived constants */
#define LVGL_PORT_AVOID_TEAR_ENABLE   (EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE)
#if LVGL_PORT_AVOID_TEAR_ENABLE
#define LVGL_PORT_AVOID_TEAR_MODE     (EXAMPLE_LVGL_PORT_AVOID_TEAR_MODE)
#define LVGL_PORT_PPA_ROTATION_ENABLE (EXAMPLE_LVGL_PORT_PPA_ROTATION_ENABLE)
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE (EXAMPLE_LVGL_PORT_ROTATION_DEGREE_)

#if LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_BUFFER_NUMS (2)
#define LVGL_PORT_FULL_REFRESH    (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_BUFFER_NUMS (3)
#define LVGL_PORT_FULL_REFRESH    (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_BUFFER_NUMS (2)
#define LVGL_PORT_DIRECT_MODE     (1)
#endif

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
#define EXAMPLE_LVGL_PORT_ROTATION_0 (1)
#else
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 90
#define EXAMPLE_LVGL_PORT_ROTATION_90  (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 180
#define EXAMPLE_LVGL_PORT_ROTATION_180 (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 270
#define EXAMPLE_LVGL_PORT_ROTATION_270 (1)
#endif
#ifdef LVGL_PORT_LCD_BUFFER_NUMS
#undef LVGL_PORT_LCD_BUFFER_NUMS
#define LVGL_PORT_LCD_BUFFER_NUMS (3)
#endif
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#else  /* !LVGL_PORT_AVOID_TEAR_ENABLE */
#define LVGL_PORT_LCD_BUFFER_NUMS (1)
#define LVGL_PORT_FULL_REFRESH    (0)
#define LVGL_PORT_DIRECT_MODE     (0)
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

/**
 * @brief Initialize LVGL port (creates LVGL task + display + touch indev).
 *        Must be called after the LCD panel and touch are fully initialized.
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                          esp_lcd_touch_handle_t tp_handle,
                          lvgl_port_interface_t  interface);

/**
 * @brief Take LVGL mutex.  timeout_ms < 0  → block forever.
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief Release LVGL mutex.
 */
void lvgl_port_unlock(void);

/**
 * @brief Called from the panel vsync ISR.  Returns true if a higher-priority
 *        task was woken (for portYIELD_FROM_ISR usage).
 */
bool lvgl_port_notify_lcd_vsync(void);

#ifdef __cplusplus
}
#endif
