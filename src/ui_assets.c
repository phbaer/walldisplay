#include "walldisplay/ui_assets.h"

#include <stdint.h>

extern const uint8_t panel_background_start[] asm("_binary_panel_background_rgb565_start");

const lv_image_dsc_t ui_panel_background = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = 480,
    .header.h = 480,
    .header.stride = 480 * 2,
    .data_size = 480 * 480 * 2,
    .data = panel_background_start,
};

#define WEATHER_IMAGE(name) \
    extern const uint8_t name##_argb8888_start[] asm("_binary_" #name "_argb8888_start"); \
    const lv_image_dsc_t ui_##name = { \
        .header.magic = LV_IMAGE_HEADER_MAGIC, \
        .header.cf = LV_COLOR_FORMAT_ARGB8888, \
        .header.flags = 0, \
        .header.w = 72, \
        .header.h = 72, \
        .header.stride = 72 * 4, \
        .data_size = 72 * 72 * 4, \
        .data = name##_argb8888_start, \
    }

WEATHER_IMAGE(weather_clear_night);
WEATHER_IMAGE(weather_cloudy);
WEATHER_IMAGE(weather_fog);
WEATHER_IMAGE(weather_lightning);
WEATHER_IMAGE(weather_lightning_rainy_day);
WEATHER_IMAGE(weather_lightning_rainy_night);
WEATHER_IMAGE(weather_partly_cloudy_day);
WEATHER_IMAGE(weather_partly_cloudy_night);
WEATHER_IMAGE(weather_pouring);
WEATHER_IMAGE(weather_rainy);
WEATHER_IMAGE(weather_snowy);
WEATHER_IMAGE(weather_snowy_rainy);
WEATHER_IMAGE(weather_sunny);
WEATHER_IMAGE(weather_unknown);
