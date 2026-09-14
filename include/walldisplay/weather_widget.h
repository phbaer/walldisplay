#pragma once
#include "lvgl.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct weather_widget weather_widget_t;
weather_widget_t *weather_widget_create(lv_obj_t *parent);
lv_obj_t *weather_widget_root(weather_widget_t *widget);
esp_err_t weather_widget_update(weather_widget_t *widget, const char *text);
/* Called by UI while holding the LVGL lock. */
void weather_widget_set_title(weather_widget_t *widget, const char *title);
#ifdef __cplusplus
}
#endif
