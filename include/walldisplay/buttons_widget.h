#pragma once
#include "lvgl.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct buttons_widget buttons_widget_t;
buttons_widget_t *buttons_widget_create(lv_obj_t *parent);
lv_obj_t *buttons_widget_root(buttons_widget_t *widget);
esp_err_t buttons_widget_update(buttons_widget_t *widget, size_t slot, const char *text);
/* Called by UI while holding the LVGL lock. */
void buttons_widget_set_title(buttons_widget_t *widget, const char *title);
#ifdef __cplusplus
}
#endif
