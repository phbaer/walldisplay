#pragma once

#include "esp_err.h"
#include "lvgl.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct media_widget media_widget_t;

media_widget_t *media_widget_create(lv_obj_t *page);
void media_widget_destroy(media_widget_t *widget);
void media_widget_set_play_label(media_widget_t *widget, lv_obj_t *play_label);
esp_err_t media_widget_update(media_widget_t *widget, const char *payload);
esp_err_t media_widget_set_artwork(media_widget_t *widget, const uint16_t *pixels, size_t width, size_t height);

#ifdef __cplusplus
}
#endif
