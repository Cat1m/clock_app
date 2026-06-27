/*
 * LVGL v9 custom allocator — routes all LVGL heap to PSRAM.
 * Required when lv_conf.h sets LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM.
 */
#include "lvgl.h"
#include "esp_heap_caps.h"

extern "C" {

void lv_mem_init(void)
{
    /* nothing — ESP-IDF initialises PSRAM heap automatically */
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lv_free_core(void *p)
{
    free(p);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

} /* extern "C" */
