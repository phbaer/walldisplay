#include "walldisplay/weather_widget.h"
#include "walldisplay/panel_component.hpp"
#include "walldisplay/ui_theme.h"
#include "walldisplay/ui_assets.h"
#include "esp_lvgl_port.h"
#include "cJSON.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <new>
#define UI_FORECAST_DAYS 3
#define UI_WEATHER_METRICS 5
#define UI_WEATHER_TREND_SAMPLES 25
#define UI_WEATHER_CURVE_POINTS ((UI_WEATHER_TREND_SAMPLES - 1) * 4 + 1)
#define UI_WEATHER_CURVE_WIDTH 294
#define UI_WEATHER_CURVE_HEIGHT 38
static bool text_contains_ci(const char *text, const char *needle) {
    if (text == NULL || needle == NULL) {
        return false;
    }

    size_t needle_len = strlen(needle);
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        size_t i = 0;
        while (i < needle_len && cursor[i] != '\0' &&
               tolower((unsigned char) cursor[i]) == tolower((unsigned char) needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

static const lv_image_dsc_t *weather_image_for_condition(const char *condition) {
    bool night = text_contains_ci(condition, "night");
    if (text_contains_ci(condition, "lightning") || text_contains_ci(condition, "thunder")) {
        if (text_contains_ci(condition, "rain")) return night ? &ui_weather_lightning_rainy_night : &ui_weather_lightning_rainy_day;
        return &ui_weather_lightning;
    }
    if (text_contains_ci(condition, "snow") && text_contains_ci(condition, "rain")) return &ui_weather_snowy_rainy;
    if (text_contains_ci(condition, "snow")) return &ui_weather_snowy;
    if (text_contains_ci(condition, "pour")) return &ui_weather_pouring;
    if (text_contains_ci(condition, "rain")) return &ui_weather_rainy;
    if (text_contains_ci(condition, "fog")) return &ui_weather_fog;
    if (text_contains_ci(condition, "partly")) return night ? &ui_weather_partly_cloudy_night : &ui_weather_partly_cloudy_day;
    if (text_contains_ci(condition, "cloud") || text_contains_ci(condition, "overcast")) return &ui_weather_cloudy;
    if (text_contains_ci(condition, "clear") || text_contains_ci(condition, "sun")) return night ? &ui_weather_clear_night : &ui_weather_sunny;
    return &ui_weather_unknown;
}

static lv_obj_t *create_weather_icon(lv_obj_t *parent, int size) {
    lv_obj_t *icon = lv_image_create(parent);
    lv_obj_set_size(icon, size, size);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(icon, &ui_weather_unknown);
    lv_image_set_scale(icon, (uint16_t) ((size * 256) / 72));
    return icon;
}

static void set_weather_icon(lv_obj_t *icon, const char *condition) {
    if (icon != NULL) lv_image_set_src(icon, weather_image_for_condition(condition));
}

struct weather_widget : PanelComponent {
    lv_obj_t *root = nullptr;
lv_obj_t *s_weather_title{};
lv_obj_t *s_weather_metric_containers[UI_WEATHER_METRICS]{};
lv_obj_t *s_weather_metric_labels[UI_WEATHER_METRICS]{};
lv_obj_t *s_weather_temperature_label{};
lv_obj_t *s_weather_curve{};
lv_point_precise_t s_weather_curve_points[UI_WEATHER_CURVE_POINTS]{};
lv_obj_t *s_weather_icons[UI_FORECAST_DAYS + 1]{};
lv_obj_t *s_forecast_day_labels[UI_FORECAST_DAYS]{};
lv_obj_t *s_forecast_temperature_labels[UI_FORECAST_DAYS]{};
lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    style_panel(page, UI_COLOR_SURFACE_ALT, 14);
    lv_obj_set_size(page, UI_CONTENT_WIDTH, UI_MAIN_COMPACT_HEIGHT);
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    s_weather_title = title;
    lv_label_set_text(title, "Weather");
    lv_obj_set_style_text_font(title, font_ui_16(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    s_weather_icons[0] = create_weather_icon(page, 72);
    lv_obj_align(s_weather_icons[0], LV_ALIGN_TOP_LEFT, 8, 52);

    s_weather_temperature_label = lv_label_create(page);
    lv_label_set_text(s_weather_temperature_label, "--.-°C");
    lv_obj_set_style_text_font(s_weather_temperature_label, font_ui_24(), 0);
    lv_obj_set_style_text_color(s_weather_temperature_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_weather_temperature_label, LV_ALIGN_TOP_LEFT, 96, 50);

    static const char *metric_symbols[UI_WEATHER_METRICS] = {
        "\xef\x81\x83", "\xef\x8f\xbd", "\xef\x9c\xae", "\xef\x9c\xbd", "\xef\x86\x85",
    };
    for (size_t i = 0; i < UI_WEATHER_METRICS; ++i) {
        s_weather_metric_containers[i] = lv_obj_create(page);
        lv_obj_remove_style_all(s_weather_metric_containers[i]);
        lv_obj_set_size(s_weather_metric_containers[i], 118, 20);
        lv_obj_add_flag(s_weather_metric_containers[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *symbol = lv_label_create(s_weather_metric_containers[i]);
        lv_label_set_text(symbol, metric_symbols[i]);
        lv_obj_set_style_text_font(symbol, font_weather_symbols_14(), 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(0xCFD5DC), 0);
        lv_obj_align(symbol, LV_ALIGN_LEFT_MID, 0, 0);

        s_weather_metric_labels[i] = lv_label_create(s_weather_metric_containers[i]);
        lv_obj_set_width(s_weather_metric_labels[i], 96);
        lv_label_set_long_mode(s_weather_metric_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_weather_metric_labels[i], font_ui_14(), 0);
        lv_obj_set_style_text_color(s_weather_metric_labels[i], lv_color_hex(0xCFD5DC), 0);
        lv_obj_align(s_weather_metric_labels[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    s_weather_curve = lv_line_create(page);
    lv_obj_set_size(s_weather_curve, UI_WEATHER_CURVE_WIDTH, UI_WEATHER_CURVE_HEIGHT);
    lv_obj_align(s_weather_curve, LV_ALIGN_TOP_LEFT, 8, 110);
    lv_obj_set_style_line_width(s_weather_curve, 3, 0);
    lv_obj_set_style_line_color(s_weather_curve, lv_color_hex(0x5FA9DD), 0);
    lv_obj_set_style_line_opa(s_weather_curve, LV_OPA_80, 0);
    lv_obj_set_style_line_rounded(s_weather_curve, true, 0);

    static const char *default_days[UI_FORECAST_DAYS] = {"Tomorrow", "+2 days", "+3 days"};
    for (size_t i = 0; i < UI_FORECAST_DAYS; ++i) {
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 132, 72);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CONTROL), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_70, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_align(card, LV_ALIGN_BOTTOM_LEFT, (int) i * 142, 0);

        s_forecast_day_labels[i] = lv_label_create(card);
        lv_label_set_text(s_forecast_day_labels[i], default_days[i]);
        lv_obj_set_width(s_forecast_day_labels[i], 116);
        lv_obj_set_style_text_font(s_forecast_day_labels[i], font_ui_14(), 0);
        lv_obj_set_style_text_color(s_forecast_day_labels[i], lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(s_forecast_day_labels[i], LV_ALIGN_TOP_LEFT, 8, 6);

        s_weather_icons[i + 1] = create_weather_icon(card, 32);
        lv_obj_align(s_weather_icons[i + 1], LV_ALIGN_BOTTOM_LEFT, 8, -4);

        s_forecast_temperature_labels[i] = lv_label_create(card);
        lv_label_set_text(s_forecast_temperature_labels[i], "--° / --°");
        lv_obj_set_style_text_font(s_forecast_temperature_labels[i], font_ui_14(), 0);
        lv_obj_set_style_text_color(s_forecast_temperature_labels[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(s_forecast_temperature_labels[i], LV_ALIGN_BOTTOM_RIGHT, -7, -10);
    }

    root = page;
    return page;
}

esp_err_t update(const char *weather_text) override {
    if (weather_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(weather_text);
    if (!cJSON_IsObject(root)) {
        for (size_t i = 0; i < UI_WEATHER_METRICS; ++i) {
            lv_obj_add_flag(s_weather_metric_containers[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(s_weather_temperature_label, "--°");
        set_weather_icon(s_weather_icons[0], weather_text);
        cJSON_Delete(root);
        lvgl_port_unlock();
        return ESP_OK;
    }

    const cJSON *temperature = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    const cJSON *humidity = cJSON_GetObjectItemCaseSensitive(root, "humidity");
    const cJSON *pressure = cJSON_GetObjectItemCaseSensitive(root, "pressure");
    const cJSON *condition = cJSON_GetObjectItemCaseSensitive(root, "condition");
    const cJSON *wind = cJSON_GetObjectItemCaseSensitive(root, "wind_speed");
    const cJSON *rain = cJSON_GetObjectItemCaseSensitive(root, "rainfall");
    const cJSON *irradiance = cJSON_GetObjectItemCaseSensitive(root, "irradiance");
    const char *condition_text = cJSON_IsString(condition) ? condition->valuestring : "Unknown";
    char temperature_text[24];
    if (cJSON_IsNumber(temperature)) {
        snprintf(temperature_text, sizeof(temperature_text), "%.1f°C", temperature->valuedouble);
    } else {
        snprintf(temperature_text, sizeof(temperature_text), "--°");
    }
    const char *wind_unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "wind_unit"));
    const char *rain_unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "rainfall_unit"));
    const char *irradiance_unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "irradiance_unit"));
    const cJSON *metric_values[UI_WEATHER_METRICS] = {humidity, pressure, wind, rain, irradiance};
    const char *metric_units[UI_WEATHER_METRICS] = {"%", "hPa", wind_unit, rain_unit, irradiance_unit};
    size_t visible_metrics = 0;
    for (size_t i = 0; i < UI_WEATHER_METRICS; ++i) {
        if (!cJSON_IsNumber(metric_values[i])) {
            lv_obj_add_flag(s_weather_metric_containers[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char metric_text[24];
        const char *unit = metric_units[i] != NULL ? metric_units[i] : "";
        if (i == 0) snprintf(metric_text, sizeof(metric_text), "%.0f%%", metric_values[i]->valuedouble);
        else if (unit[0] != '\0') snprintf(metric_text, sizeof(metric_text), "%.0f %s", metric_values[i]->valuedouble, unit);
        else snprintf(metric_text, sizeof(metric_text), "%.0f", metric_values[i]->valuedouble);
        lv_label_set_text(s_weather_metric_labels[i], metric_text);
        lv_obj_align(s_weather_metric_containers[i], LV_ALIGN_TOP_LEFT, 310, 48 + (int) (visible_metrics++ * 20));
        lv_obj_clear_flag(s_weather_metric_containers[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_weather_temperature_label, temperature_text);
    set_weather_icon(s_weather_icons[0], condition_text);

    const cJSON *forecast = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    for (size_t i = 0; i < UI_FORECAST_DAYS; ++i) {
        const cJSON *entry = cJSON_IsArray(forecast) ? cJSON_GetArrayItem(forecast, (int) i) : NULL;
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(entry, "day");
        const cJSON *entry_condition = cJSON_GetObjectItemCaseSensitive(entry, "condition");
        const cJSON *high = cJSON_GetObjectItemCaseSensitive(entry, "high");
        const cJSON *low = cJSON_GetObjectItemCaseSensitive(entry, "low");
        if (cJSON_IsString(day)) {
            lv_label_set_text(s_forecast_day_labels[i], day->valuestring);
        }
        const char *entry_condition_text = cJSON_IsString(entry_condition) ? entry_condition->valuestring : "unknown";
        set_weather_icon(s_weather_icons[i + 1], entry_condition_text);
        if (cJSON_IsNumber(high) && cJSON_IsNumber(low)) {
            char range[24];
            snprintf(range, sizeof(range), "%.0f° / %.0f°", high->valuedouble, low->valuedouble);
            lv_label_set_text(s_forecast_temperature_labels[i], range);
        }
    }

    const cJSON *trend = cJSON_GetObjectItemCaseSensitive(root, "trend");
    float trend_values[UI_WEATHER_TREND_SAMPLES];
    size_t trend_count = 0;
    if (cJSON_IsArray(trend)) {
        const int entries = cJSON_GetArraySize(trend);
        for (int i = 0; i < entries && trend_count < UI_WEATHER_TREND_SAMPLES; ++i) {
            const cJSON *entry = cJSON_GetArrayItem(trend, i);
            if (cJSON_IsNumber(entry)) trend_values[trend_count++] = (float) entry->valuedouble;
        }
    }
    if (s_weather_curve != NULL && trend_count <= 1) {
        lv_obj_add_flag(s_weather_curve, LV_OBJ_FLAG_HIDDEN);
    } else if (s_weather_curve != NULL) {
        float minimum = trend_values[0], maximum = trend_values[0];
        for (size_t i = 1; i < trend_count; ++i) {
            if (trend_values[i] < minimum) minimum = trend_values[i];
            if (trend_values[i] > maximum) maximum = trend_values[i];
        }
        const float padding = (maximum - minimum) * 0.12f < 1.0f ? 1.0f : (maximum - minimum) * 0.12f;
        minimum -= padding;
        maximum += padding;
        const float range = maximum - minimum;
        size_t point_count = 0;
        for (size_t segment = 0; segment + 1 < trend_count; ++segment) {
            const float p0 = trend_values[segment == 0 ? 0 : segment - 1];
            const float p1 = trend_values[segment];
            const float p2 = trend_values[segment + 1];
            const float p3 = trend_values[segment + 2 < trend_count ? segment + 2 : trend_count - 1];
            for (int step = 0; step < 4; ++step) {
                const float t = (float) step / 4.0f;
                const float t2 = t * t;
                const float t3 = t2 * t;
                float value = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
                if (value < minimum) value = minimum;
                if (value > maximum) value = maximum;
                s_weather_curve_points[point_count].x = (lv_coord_t) (point_count * (UI_WEATHER_CURVE_WIDTH - 8) / ((trend_count - 1) * 4));
                s_weather_curve_points[point_count++].y = (lv_coord_t) ((UI_WEATHER_CURVE_HEIGHT - 4) - (value - minimum) * (UI_WEATHER_CURVE_HEIGHT - 10) / range);
            }
        }
        s_weather_curve_points[point_count].x = UI_WEATHER_CURVE_WIDTH - 8;
        s_weather_curve_points[point_count++].y = (lv_coord_t) ((UI_WEATHER_CURVE_HEIGHT - 4) - (trend_values[trend_count - 1] - minimum) * (UI_WEATHER_CURVE_HEIGHT - 10) / range);
        lv_line_set_points(s_weather_curve, s_weather_curve_points, point_count);
        lv_obj_clear_flag(s_weather_curve, LV_OBJ_FLAG_HIDDEN);
    }

    cJSON_Delete(root);
    lvgl_port_unlock();
    return ESP_OK;
}


    void set_visible(bool visible) override {
        if (visible) lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
};
extern "C" weather_widget_t *weather_widget_create(lv_obj_t *parent) {
    auto *widget = new (std::nothrow) weather_widget{};
    if (widget) widget->create(parent);
    return widget;
}
extern "C" lv_obj_t *weather_widget_root(weather_widget_t *widget) { return widget->root; }
extern "C" esp_err_t weather_widget_update(weather_widget_t *widget, const char *text) { return widget->update(text); }
extern "C" void weather_widget_set_title(weather_widget_t *widget, const char *title) {
    lv_obj_set_width(widget->s_weather_title, 370);
    lv_label_set_long_mode(widget->s_weather_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->s_weather_title, title);
}
