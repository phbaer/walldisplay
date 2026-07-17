#include "walldisplay/ui.h"

#include "walldisplay/app_config.h"
#include "walldisplay/display_dimming.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "cJSON.h"
#include "walldisplay/mqtt_app.h"
#include "walldisplay/media_widget.h"
#include "walldisplay/ui_assets.h"
#include "walldisplay/ui_font_noto_16.h"
#include "walldisplay/ui_font_temperature_28_bold.h"
#include "walldisplay/ui_font_weather_symbols_14.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define UI_MAX_DYNAMIC_BUTTONS 5
#define UI_MAX_MEASUREMENT_CHIPS 4
#define UI_MAX_MEDIA_FAVORITES 5

#define UI_SCREEN_MARGIN 10
#define UI_CONTENT_WIDTH 460
#define UI_HEADER_HEIGHT 112
#define UI_MAIN_HEIGHT 242
#define UI_FOOTER_COMPACT_HEIGHT 76
#define UI_MAIN_COMPACT_HEIGHT 254
#define UI_MAIN_FULL_HEIGHT 340
#define UI_GAP 10
#define UI_FORECAST_DAYS 3
#define UI_WEATHER_METRICS 5
#define UI_WEATHER_TREND_SAMPLES 25
#define UI_WEATHER_CURVE_POINTS ((UI_WEATHER_TREND_SAMPLES - 1) * 4 + 1)
#define UI_WEATHER_CURVE_WIDTH 294
#define UI_WEATHER_CURVE_HEIGHT 38
#define UI_STATUS_CHIP_WIDTH 36
#define UI_MEASUREMENT_CHIP_WIDTH 82
#define UI_STATUS_CHIP_ROW_WIDTH (3 * UI_STATUS_CHIP_WIDTH)
#define UI_MEASUREMENT_CHIP_ROW_WIDTH (UI_MAX_MEASUREMENT_CHIPS * UI_MEASUREMENT_CHIP_WIDTH)

#define UI_COLOR_SURFACE 0x0C0D10
#define UI_COLOR_SURFACE_ALT 0x101317
#define UI_COLOR_CONTROL 0x1A1F26
#define UI_COLOR_CONTROL_PRESSED 0x2B323C
#define UI_COLOR_BORDER 0x252B33
#define UI_COLOR_TEXT 0xF2F2F2
#define UI_COLOR_TEXT_MUTED 0xA4ACB8

static const char *TAG = "ui";
static lv_obj_t *s_title_label;
static lv_obj_t *s_media_play_label;
static lv_obj_t *s_media_volume_slider;
static media_widget_t *s_media_widget;
static lv_obj_t *s_main_area;
static lv_obj_t *s_weather_page;
static lv_obj_t *s_media_page;
static lv_obj_t *s_footer;
static lv_obj_t *s_dynamic_row;
static lv_obj_t *s_weather_metric_containers[UI_WEATHER_METRICS];
static lv_obj_t *s_weather_metric_labels[UI_WEATHER_METRICS];
static lv_obj_t *s_weather_temperature_label;
static lv_obj_t *s_weather_curve;
static lv_point_precise_t s_weather_curve_points[UI_WEATHER_CURVE_POINTS];
static lv_obj_t *s_weather_icons[UI_FORECAST_DAYS + 1];
static lv_obj_t *s_forecast_day_labels[UI_FORECAST_DAYS];
static lv_obj_t *s_forecast_temperature_labels[UI_FORECAST_DAYS];
static lv_obj_t *s_clock_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_wifi_chip;
static lv_obj_t *s_mqtt_chip;
static lv_obj_t *s_ha_chip;
static lv_obj_t *s_wifi_chip_label;
static lv_obj_t *s_mqtt_chip_label;
static lv_obj_t *s_ha_chip_label;
static lv_obj_t *s_measurement_chips[UI_MAX_MEASUREMENT_CHIPS];
static lv_obj_t *s_measurement_chip_labels[UI_MAX_MEASUREMENT_CHIPS];
static lv_obj_t *s_dynamic_buttons[UI_MAX_DYNAMIC_BUTTONS];
static lv_obj_t *s_dynamic_button_labels[UI_MAX_DYNAMIC_BUTTONS];
static lv_obj_t *s_dynamic_button_switches[UI_MAX_DYNAMIC_BUTTONS];
static int s_dynamic_button_slots[UI_MAX_DYNAMIC_BUTTONS];
static lv_obj_t *s_media_favorite_buttons[UI_MAX_MEDIA_FAVORITES];
static lv_obj_t *s_media_favorite_labels[UI_MAX_MEDIA_FAVORITES];
static lv_obj_t *s_media_favorite_icons[UI_MAX_MEDIA_FAVORITES];
static int s_media_favorite_slots[UI_MAX_MEDIA_FAVORITES];
static void dynamic_button_event_cb(lv_event_t *event);
static void media_control_event_cb(lv_event_t *event);
static void media_volume_event_cb(lv_event_t *event);
static void media_favorite_event_cb(lv_event_t *event);

static void touch_activity_event_cb(lv_event_t *event) {
    LV_UNUSED(event);
    display_dimming_wake();
}
static void page_switch_event_cb(lv_event_t *event);

/* Static Noto Sans renders all UI text without runtime glyph allocation. */
static const lv_font_t *font_ui_14(void) { return &ui_font_noto_16; }
static const lv_font_t *font_ui_16(void) { return &ui_font_noto_16; }
static const lv_font_t *font_ui_20(void) { return &ui_font_noto_16; }
static const lv_font_t *font_ui_24(void) { return &ui_font_temperature_28_bold; }
static const lv_font_t *font_time(void) { return &lv_font_montserrat_28; }

/* LVGL's private-use LV_SYMBOL_* glyphs are supplied by Montserrat. */
static const lv_font_t *font_symbols_14(void) { return &lv_font_montserrat_14; }
static const lv_font_t *font_weather_symbols_14(void) { return &ui_font_weather_symbols_14; }

static void style_panel(lv_obj_t *object, uint32_t color, int radius) {
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(object, radius, 0);
}

static void style_button(lv_obj_t *button) {
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_CONTROL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x234E68), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(button, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2D3540), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(0x425161), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
}

static void style_volume_rocker_button(lv_obj_t *button) {
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x285875), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
}

static lv_obj_t *create_switch_indicator(lv_obj_t *button) {
    lv_obj_t *track = lv_obj_create(button);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, 36, 20);
    lv_obj_set_style_radius(track, 10, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x59636F), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(track, LV_ALIGN_RIGHT_MID, -7, 0);

    lv_obj_t *knob = lv_obj_create(track);
    lv_obj_remove_style_all(knob);
    lv_obj_set_size(knob, 16, 16);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, lv_color_hex(0xE6ECF4), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_remove_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(knob, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
    return track;
}

static lv_obj_t *create_state_segment(lv_obj_t *parent, int x_offset, const char *icon, lv_obj_t **out_label) {
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_remove_style_all(segment);
    lv_obj_set_size(segment, UI_STATUS_CHIP_WIDTH, 24);
    lv_obj_set_style_bg_color(segment, lv_color_hex(0x1A1F26), 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
    if (x_offset > 0) {
        lv_obj_set_style_border_width(segment, 1, 0);
        lv_obj_set_style_border_side(segment, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(segment, lv_color_hex(0x2A3038), 0);
    }
    lv_obj_align(segment, LV_ALIGN_LEFT_MID, x_offset, 0);

    lv_obj_t *label = lv_label_create(segment);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, font_symbols_14(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE6ECF4), 0);
    lv_obj_center(label);
    *out_label = label;

    return segment;
}

static lv_obj_t *create_measurement_chip(lv_obj_t *parent, const char *initial_text, lv_obj_t **out_label) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, UI_MEASUREMENT_CHIP_WIDTH, 24);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x171B21), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_radius(chip, 12, 0);

    lv_obj_t *label = lv_label_create(chip);
    lv_label_set_text(label, initial_text);
    lv_obj_set_style_text_font(label, font_ui_14(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xD7DEE8), 0);
    lv_obj_center(label);

    if (out_label != NULL) {
        *out_label = label;
    }

    return chip;
}

static bool string_equals_ci(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
            return false;
        }
        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool parse_hex_color(const char *text, lv_color_t *out) {
    if (text == NULL || out == NULL || text[0] != '#') {
        return false;
    }

    if (strlen(text) != 7) {
        return false;
    }

    int r_hi = hex_value(text[1]);
    int r_lo = hex_value(text[2]);
    int g_hi = hex_value(text[3]);
    int g_lo = hex_value(text[4]);
    int b_hi = hex_value(text[5]);
    int b_lo = hex_value(text[6]);
    if (r_hi < 0 || r_lo < 0 || g_hi < 0 || g_lo < 0 || b_hi < 0 || b_lo < 0) {
        return false;
    }

    uint8_t r = (uint8_t) ((r_hi << 4) | r_lo);
    uint8_t g = (uint8_t) ((g_hi << 4) | g_lo);
    uint8_t b = (uint8_t) ((b_hi << 4) | b_lo);
    *out = lv_color_make(r, g, b);
    return true;
}

static lv_color_t measurement_chip_color_from_text(const char *color_text) {
    lv_color_t parsed;
    if (parse_hex_color(color_text, &parsed)) {
        return parsed;
    }

    if (color_text == NULL || color_text[0] == '\0' || string_equals_ci(color_text, "neutral")) {
        return lv_color_hex(0x171B21);
    }

    if (string_equals_ci(color_text, "ok") || string_equals_ci(color_text, "green")) {
        return lv_color_hex(0x2B8A3E);
    }

    if (string_equals_ci(color_text, "warn") || string_equals_ci(color_text, "warning") ||
        string_equals_ci(color_text, "amber") || string_equals_ci(color_text, "yellow")) {
        return lv_color_hex(0xA67E34);
    }

    if (string_equals_ci(color_text, "alert") || string_equals_ci(color_text, "alarm") ||
        string_equals_ci(color_text, "red") || string_equals_ci(color_text, "error")) {
        return lv_color_hex(0x9F2E3A);
    }

    if (string_equals_ci(color_text, "blue")) {
        return lv_color_hex(0x2D7FB8);
    }

    return lv_color_hex(0x171B21);
}

static esp_err_t set_measurement_chip_color_locked(size_t index, const char *color_text) {
    if (index >= UI_MAX_MEASUREMENT_CHIPS || color_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *chip = s_measurement_chips[index];
    if (chip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_color_t color = measurement_chip_color_from_text(color_text);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_set_style_border_color(chip, color, 0);
    return ESP_OK;
}

static lv_color_t chip_color_from_state(const char *state_text) {
    if (state_text == NULL) {
        return lv_color_hex(0xA67E34);
    }

    if (strstr(state_text, "ok") != NULL || strstr(state_text, "online") != NULL) {
        return lv_color_hex(0x2B8A3E);
    }

    if (strstr(state_text, "...") != NULL || strstr(state_text, "connecting") != NULL) {
        return lv_color_hex(0xA67E34);
    }

    return lv_color_hex(0x9F2E3A);
}

static esp_err_t set_chip_state_locked(lv_obj_t *chip, lv_obj_t *chip_label, const char *state_text) {
    if (chip == NULL || chip_label == NULL || state_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_color_t state_color = chip_color_from_state(state_text);
    lv_obj_set_style_bg_color(chip, state_color, 0);
    lv_obj_set_style_border_color(chip, state_color, 0);

    return ESP_OK;
}

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

static void create_page_switch(lv_obj_t *page, const char *symbol) {
    lv_obj_t *button = lv_btn_create(page);
    style_button(button);
    lv_obj_set_size(button, 38, 34);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(button, page_switch_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, font_symbols_14(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_center(label);
}

static esp_err_t publish_dynamic_button_action(size_t index) {
    const app_config_t *config = app_config_get();
    char command_topic[APP_TOPIC_MAX_LEN + 32];
    char state_topic[APP_TOPIC_MAX_LEN + 32];

    if (index >= UI_MAX_DYNAMIC_BUTTONS) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(command_topic, sizeof(command_topic), "%s/cmd/button%u", config->base_topic, (unsigned) (index + 1));
    if (mqtt_app_publish_async(command_topic, "toggle", false) != ESP_OK) {
        return ESP_FAIL;
    }

    snprintf(state_topic, sizeof(state_topic), "%s/state/button%u_action", config->base_topic, (unsigned) (index + 1));
    if (mqtt_app_publish_async(state_topic, "toggle", true) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t publish_media_command(const char *command) {
    const app_config_t *config = app_config_get();
    char topic[APP_TOPIC_MAX_LEN + 40];
    snprintf(topic, sizeof(topic), "%s/cmd/media/%s", config->base_topic, command);
    return mqtt_app_publish_async(topic, "press", false);
}

static esp_err_t publish_media_volume(int volume_percent) {
    const app_config_t *config = app_config_get();
    char topic[APP_TOPIC_MAX_LEN + 40];
    char payload[4];
    snprintf(topic, sizeof(topic), "%s/cmd/media/volume", config->base_topic);
    snprintf(payload, sizeof(payload), "%d", volume_percent < 0 ? 0 : volume_percent > 100 ? 100 : volume_percent);
    return mqtt_app_publish_async(topic, payload, false);
}

static lv_obj_t *create_media_button(lv_obj_t *parent, const char *text, int width, int height, void *user_data,
                                     lv_event_cb_t callback, bool symbols) {
    lv_obj_t *button = lv_btn_create(parent);
    style_button(button);
    lv_obj_set_size(button, width, height);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, symbols ? font_symbols_14() : font_ui_14(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *create_main_page(lv_obj_t *parent, const char *title, const char *switch_text,
                                  const char *placeholder, lv_obj_t **out_value_label) {
    lv_obj_t *page = lv_obj_create(parent);
    style_panel(page, UI_COLOR_SURFACE_ALT, 14);
    lv_obj_set_size(page, UI_CONTENT_WIDTH, UI_MAIN_HEIGHT);
    lv_obj_set_style_pad_all(page, 16, 0);

    lv_obj_t *title_label = lv_label_create(page);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_ui_16(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xA4ACB8), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    create_page_switch(page, switch_text);

    lv_obj_t *value = lv_label_create(page);
    lv_label_set_text(value, placeholder);
    lv_obj_set_width(value, 424);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, font_ui_20(), 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xECECEC), 0);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 0, 42);

    if (out_value_label != NULL) {
        *out_value_label = value;
    }
    return page;
}

static lv_obj_t *create_weather_page(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    style_panel(page, UI_COLOR_SURFACE_ALT, 14);
    lv_obj_set_size(page, UI_CONTENT_WIDTH, UI_MAIN_COMPACT_HEIGHT);
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Weather");
    lv_obj_set_style_text_font(title, font_ui_16(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);
    create_page_switch(page, LV_SYMBOL_RIGHT);

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

    return page;
}

static esp_err_t set_label_text_locked(lv_obj_t *label, const char *text) {
    if (label == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_label_set_text(label, text);
    return ESP_OK;
}

static void set_toggle_visual(lv_obj_t *track, bool active) {
    if (active) {
        lv_obj_set_style_bg_color(track, lv_color_hex(0x2D8BC0), 0);
        lv_obj_align(lv_obj_get_child(track, 0), LV_ALIGN_RIGHT_MID, -2, 0);
    } else {
        lv_obj_set_style_bg_color(track, lv_color_hex(0x59636F), 0);
        lv_obj_align(lv_obj_get_child(track, 0), LV_ALIGN_LEFT_MID, 2, 0);
    }
}

static void dynamic_button_event_cb(lv_event_t *event) {
    const int *slot_ptr = lv_event_get_user_data(event);
    if (slot_ptr == NULL) {
        return;
    }

    const size_t slot = (size_t) *slot_ptr;
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && publish_dynamic_button_action(slot) != ESP_OK) {
        ESP_LOGW(TAG, "Button %u command publish failed", (unsigned) (slot + 1));
    }
}

static void media_control_event_cb(lv_event_t *event) {
    const char *command = lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) && publish_media_command(command) != ESP_OK) {
        ESP_LOGW(TAG, "Media command publish failed");
    }
}

static void media_volume_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_RELEASED &&
        publish_media_volume(lv_slider_get_value(lv_event_get_target(event))) != ESP_OK) {
        ESP_LOGW(TAG, "Media volume publish failed");
    }
}

static void media_favorite_event_cb(lv_event_t *event) {
    const int *slot = lv_event_get_user_data(event);
    if (slot == NULL || *slot < 0 || *slot >= UI_MAX_MEDIA_FAVORITES || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    char command[16];
    snprintf(command, sizeof(command), "favorite%u", (unsigned)*slot + 1U);
    if (publish_media_command(command) != ESP_OK) ESP_LOGW(TAG, "Media favourite publish failed");
}

static const char *media_favorite_symbol(const char *icon_name) {
    if (icon_name == NULL || string_equals_ci(icon_name, "none")) return "";
    if (string_equals_ci(icon_name, "radio") || string_equals_ci(icon_name, "music")) return LV_SYMBOL_AUDIO;
    if (string_equals_ci(icon_name, "album")) return LV_SYMBOL_IMAGE;
    if (string_equals_ci(icon_name, "playlist")) return LV_SYMBOL_LIST;
    if (string_equals_ci(icon_name, "podcast")) return LV_SYMBOL_LOOP;
    return "";
}

static void page_switch_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_weather_page == NULL || s_media_page == NULL) {
        return;
    }

    lv_obj_t *next = lv_obj_has_flag(s_weather_page, LV_OBJ_FLAG_HIDDEN) ? s_weather_page : s_media_page;
    lv_obj_t *current = next == s_weather_page ? s_media_page : s_weather_page;
    lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next, LV_OBJ_FLAG_HIDDEN);
}

esp_err_t ui_show_page(const char *page_name) {
    if (page_name == NULL || !lvgl_port_lock(0)) return ESP_ERR_INVALID_ARG;
    lv_obj_t *next = string_equals_ci(page_name, "weather") ? s_weather_page :
                     string_equals_ci(page_name, "media") ? s_media_page : NULL;
    if (next == NULL || s_weather_page == NULL || s_media_page == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    lv_obj_add_flag(next == s_weather_page ? s_media_page : s_weather_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    return ESP_OK;
}

static void update_footer_layout_locked(void) {
    size_t visible_buttons = 0;
    for (size_t i = 0; i < UI_MAX_DYNAMIC_BUTTONS; ++i) {
        if (s_dynamic_buttons[i] != NULL && !lv_obj_has_flag(s_dynamic_buttons[i], LV_OBJ_FLAG_HIDDEN)) {
            ++visible_buttons;
        }
    }

    bool footer_hidden = visible_buttons == 0;
    int main_height = footer_hidden ? UI_MAIN_FULL_HEIGHT : UI_MAIN_COMPACT_HEIGHT;
    lv_obj_set_height(s_main_area, main_height);
    lv_obj_set_height(s_weather_page, main_height);
    lv_obj_set_height(s_media_page, main_height);
    lv_obj_set_height(s_footer, UI_FOOTER_COMPACT_HEIGHT);
    if (footer_hidden) {
        lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_dynamic_row, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static esp_err_t set_label_text(lv_obj_t *label, const char *text) {
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }
    esp_err_t ret = set_label_text_locked(label, text);
    lvgl_port_unlock();
    return ret;
}

esp_err_t ui_init(const display_board_handle_t *board) {
    if (board == NULL || !lv_is_initialized()) {
        ESP_LOGW(TAG, "LVGL is not initialized yet; UI scaffold skipped");
        return ESP_OK;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    lv_obj_t *screen = lv_scr_act();
    if (board->touch != NULL) {
        lv_indev_add_event_cb(board->touch, touch_activity_event_cb, LV_EVENT_PRESSED, NULL);
    }
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *background = lv_image_create(screen);
    lv_image_set_src(background, &ui_panel_background);
    lv_obj_center(background);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *header = lv_obj_create(screen);
    style_panel(header, UI_COLOR_SURFACE, 12);
    lv_obj_set_size(header, UI_CONTENT_WIDTH, UI_HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, UI_SCREEN_MARGIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_clock_label = lv_label_create(header);
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_set_style_text_font(s_clock_label, font_time(), 0);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_clock_label, LV_ALIGN_TOP_LEFT, 14, 14);

    s_title_label = lv_label_create(header);
    lv_label_set_text(s_title_label, "Living Room");
    lv_obj_set_width(s_title_label, 300);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_title_label, font_ui_14(), 0);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_LEFT, 14, 48);

    s_date_label = lv_label_create(header);
    lv_label_set_text(s_date_label, "---, -- ---");
    lv_obj_set_style_text_font(s_date_label, font_time(), 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_RIGHT, -14, 14);

    lv_obj_t *chip_row = lv_obj_create(header);
    lv_obj_remove_style_all(chip_row);
    lv_obj_set_size(chip_row, 436, 30);
    lv_obj_align(chip_row, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *status_control = lv_obj_create(chip_row);
    lv_obj_remove_style_all(status_control);
    lv_obj_set_size(status_control, UI_STATUS_CHIP_ROW_WIDTH, 24);
    lv_obj_set_style_bg_color(status_control, lv_color_hex(0x1A1F26), 0);
    lv_obj_set_style_bg_opa(status_control, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_control, 1, 0);
    lv_obj_set_style_border_color(status_control, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_radius(status_control, 12, 0);
    lv_obj_set_style_clip_corner(status_control, true, 0);
    lv_obj_align(status_control, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *measurement_chip_row = lv_obj_create(chip_row);
    lv_obj_remove_style_all(measurement_chip_row);
    lv_obj_set_size(measurement_chip_row, UI_MEASUREMENT_CHIP_ROW_WIDTH, 30);
    lv_obj_set_layout(measurement_chip_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(measurement_chip_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(measurement_chip_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(measurement_chip_row, LV_ALIGN_LEFT_MID, 0, 0);

    s_measurement_chips[0] = create_measurement_chip(measurement_chip_row, "--", &s_measurement_chip_labels[0]);
    s_measurement_chips[1] = create_measurement_chip(measurement_chip_row, "--", &s_measurement_chip_labels[1]);
    s_measurement_chips[2] = create_measurement_chip(measurement_chip_row, "--", &s_measurement_chip_labels[2]);
    s_measurement_chips[3] = create_measurement_chip(measurement_chip_row, "--", &s_measurement_chip_labels[3]);
    for (size_t i = 0; i < UI_MAX_MEASUREMENT_CHIPS; ++i) {
        lv_obj_add_flag(s_measurement_chips[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_wifi_chip = create_state_segment(status_control, 0, LV_SYMBOL_WIFI, &s_wifi_chip_label);
    s_mqtt_chip = create_state_segment(status_control, UI_STATUS_CHIP_WIDTH, LV_SYMBOL_UPLOAD, &s_mqtt_chip_label);
    s_ha_chip = create_state_segment(status_control, 2 * UI_STATUS_CHIP_WIDTH, LV_SYMBOL_HOME, &s_ha_chip_label);
    set_measurement_chip_color_locked(0, "neutral");
    set_measurement_chip_color_locked(1, "neutral");
    set_measurement_chip_color_locked(2, "neutral");
    set_measurement_chip_color_locked(3, "neutral");

    set_chip_state_locked(s_wifi_chip, s_wifi_chip_label, "...");
    set_chip_state_locked(s_mqtt_chip, s_mqtt_chip_label, "...");
    set_chip_state_locked(s_ha_chip, s_ha_chip_label, "...");

    s_main_area = lv_obj_create(screen);
    lv_obj_remove_style_all(s_main_area);
    lv_obj_set_size(s_main_area, UI_CONTENT_WIDTH, UI_MAIN_COMPACT_HEIGHT);
    lv_obj_clear_flag(s_main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_main_area, LV_ALIGN_TOP_MID, 0, UI_SCREEN_MARGIN + UI_HEADER_HEIGHT + UI_GAP);

    s_weather_page = create_weather_page(s_main_area);
    s_media_page = create_main_page(s_main_area, "", LV_SYMBOL_LEFT, "", NULL);
    s_media_widget = media_widget_create(s_media_page);
    if (s_media_widget == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_obj_t *media_controls = lv_obj_create(s_media_page);
    lv_obj_remove_style_all(media_controls);
    lv_obj_set_size(media_controls, 428, 54);
    lv_obj_set_layout(media_controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(media_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(media_controls, 8, 0);
    lv_obj_align(media_controls, LV_ALIGN_BOTTOM_MID, 0, -42);
    create_media_button(media_controls, LV_SYMBOL_PREV, 56, 52, "previous", media_control_event_cb, true);
    lv_obj_t *play_button = create_media_button(media_controls, LV_SYMBOL_PLAY, 56, 52, "play_pause", media_control_event_cb, true);
    s_media_play_label = lv_obj_get_child(play_button, 0);
    media_widget_set_play_label(s_media_widget, s_media_play_label);
    create_media_button(media_controls, LV_SYMBOL_NEXT, 56, 52, "next", media_control_event_cb, true);

    lv_obj_t *volume_rocker = lv_obj_create(media_controls);
    style_panel(volume_rocker, 0x112536, 14);
    lv_obj_set_size(volume_rocker, 236, 52);
    lv_obj_set_style_pad_all(volume_rocker, 4, 0);
    lv_obj_set_style_clip_corner(volume_rocker, true, 0);
    lv_obj_t *volume_down_button = create_media_button(volume_rocker, LV_SYMBOL_MINUS, 40, 44, "volume_down", media_control_event_cb, true);
    style_volume_rocker_button(volume_down_button);
    lv_obj_align(volume_down_button, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_event_cb(volume_down_button, media_control_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, "volume_down");

    s_media_volume_slider = lv_slider_create(volume_rocker);
    lv_slider_set_range(s_media_volume_slider, 0, 100);
    lv_slider_set_value(s_media_volume_slider, 50, LV_ANIM_OFF);
    lv_obj_set_size(s_media_volume_slider, 140, 14);
    lv_obj_set_style_bg_color(s_media_volume_slider, lv_color_hex(0x1B3E57), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_media_volume_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_media_volume_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_media_volume_slider, lv_color_hex(0x5FA9DD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_media_volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_media_volume_slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_media_volume_slider, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_border_color(s_media_volume_slider, lv_color_hex(0x5FA9DD), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_media_volume_slider, 2, LV_PART_KNOB);
    lv_obj_align(s_media_volume_slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_media_volume_slider, media_volume_event_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *volume_up_button = create_media_button(volume_rocker, LV_SYMBOL_PLUS, 40, 44, "volume_up", media_control_event_cb, true);
    style_volume_rocker_button(volume_up_button);
    lv_obj_align(volume_up_button, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(volume_up_button, media_control_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, "volume_up");
    lv_obj_t *media_favorites = lv_obj_create(s_media_page);
    lv_obj_remove_style_all(media_favorites);
    lv_obj_set_size(media_favorites, 424, 34);
    lv_obj_set_layout(media_favorites, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(media_favorites, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(media_favorites, 6, 0);
    lv_obj_align(media_favorites, LV_ALIGN_BOTTOM_MID, 0, 0);
    for (size_t i = 0; i < UI_MAX_MEDIA_FAVORITES; ++i) {
        s_media_favorite_slots[i] = (int)i;
        lv_obj_t *button = create_media_button(media_favorites, "", 0, 30, &s_media_favorite_slots[i], media_favorite_event_cb, false);
        lv_obj_set_flex_grow(button, 1);
        s_media_favorite_buttons[i] = button;
        s_media_favorite_labels[i] = lv_obj_get_child(button, 0);
        lv_obj_set_width(s_media_favorite_labels[i], LV_PCT(100));
        lv_label_set_long_mode(s_media_favorite_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_center(s_media_favorite_labels[i]);
        s_media_favorite_icons[i] = lv_label_create(button);
        lv_obj_set_style_text_font(s_media_favorite_icons[i], font_symbols_14(), 0);
        lv_obj_set_style_text_color(s_media_favorite_icons[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(s_media_favorite_icons[i], LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_add_flag(s_media_favorite_icons[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_media_page, LV_OBJ_FLAG_HIDDEN);

    s_footer = lv_obj_create(screen);
    style_panel(s_footer, UI_COLOR_SURFACE, 12);
    lv_obj_set_size(s_footer, UI_CONTENT_WIDTH, UI_FOOTER_COMPACT_HEIGHT);
    lv_obj_set_style_pad_all(s_footer, 8, 0);
    lv_obj_set_layout(s_footer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_footer, 8, 0);
    lv_obj_set_flex_align(s_footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_dynamic_row = lv_obj_create(s_footer);
    lv_obj_remove_style_all(s_dynamic_row);
    lv_obj_set_size(s_dynamic_row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_dynamic_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_dynamic_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_dynamic_row, 8, 0);
    lv_obj_set_flex_align(s_dynamic_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (size_t i = 0; i < UI_MAX_DYNAMIC_BUTTONS; ++i) {
        s_dynamic_button_slots[i] = (int) i;
        lv_obj_t *button = lv_btn_create(s_dynamic_row);
        style_button(button);
        lv_obj_set_size(button, 0, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
        lv_obj_add_event_cb(button, dynamic_button_event_cb, LV_EVENT_CLICKED, &s_dynamic_button_slots[i]);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, "-");
        lv_obj_set_style_text_font(label, font_ui_14(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xE5EAF0), 0);
        lv_obj_center(label);

        s_dynamic_button_switches[i] = create_switch_indicator(button);

        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
        s_dynamic_buttons[i] = button;
        s_dynamic_button_labels[i] = label;
    }

    update_footer_layout_locked();

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI scaffold created");
    return ESP_OK;
}

esp_err_t ui_set_connection_status(const char *status_text) {
    /* MQTT availability remains functional, but no status text is rendered. */
    return status_text == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t ui_set_title_text(const char *title_text) {
    return set_label_text(s_title_label, title_text);
}

esp_err_t ui_set_weather_text(const char *weather_text) {
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

esp_err_t ui_set_media_text(const char *media_text) {
    if (media_text == NULL) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(media_text);
    const cJSON *volume = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "volume_level") : NULL;
    if (cJSON_IsNumber(volume) && s_media_volume_slider != NULL && lvgl_port_lock(0)) {
        int volume_percent = (int) (volume->valuedouble * 100.0 + 0.5);
        lv_slider_set_value(s_media_volume_slider, volume_percent < 0 ? 0 : volume_percent > 100 ? 100 : volume_percent, LV_ANIM_OFF);
        lvgl_port_unlock();
    }
    cJSON_Delete(root);
    return media_widget_update(s_media_widget, media_text);
}

esp_err_t ui_set_media_favorite_label(size_t index, const char *label_text) {
    if (index >= UI_MAX_MEDIA_FAVORITES || label_text == NULL || !lvgl_port_lock(0)) return ESP_ERR_INVALID_ARG;
    if (label_text[0] == '\0') lv_obj_add_flag(s_media_favorite_buttons[index], LV_OBJ_FLAG_HIDDEN);
    else { lv_label_set_text(s_media_favorite_labels[index], label_text); lv_obj_clear_flag(s_media_favorite_buttons[index], LV_OBJ_FLAG_HIDDEN); }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_media_favorite_icon(size_t index, const char *icon_name) {
    if (index >= UI_MAX_MEDIA_FAVORITES || icon_name == NULL || !lvgl_port_lock(0)) return ESP_ERR_INVALID_ARG;
    const char *symbol = media_favorite_symbol(icon_name);
    if (symbol[0] == '\0') {
        lv_obj_add_flag(s_media_favorite_icons[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_media_favorite_labels[index], LV_PCT(100));
        lv_obj_center(s_media_favorite_labels[index]);
    } else {
        lv_label_set_text(s_media_favorite_icons[index], symbol);
        lv_obj_clear_flag(s_media_favorite_icons[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_media_favorite_labels[index], LV_PCT(70));
        lv_obj_align(s_media_favorite_labels[index], LV_ALIGN_RIGHT_MID, -3, 0);
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_media_artwork(const uint16_t *pixels, size_t width, size_t height) {
    return media_widget_set_artwork(s_media_widget, pixels, width, height);
}

esp_err_t ui_set_clock_text(const char *clock_text) {
    return set_label_text(s_clock_label, clock_text);
}

esp_err_t ui_set_date_text(const char *date_text) {
    return set_label_text(s_date_label, date_text);
}

esp_err_t ui_set_wifi_state(const char *state_text) {
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }
    esp_err_t ret = set_chip_state_locked(s_wifi_chip, s_wifi_chip_label, state_text);
    lvgl_port_unlock();
    return ret;
}

esp_err_t ui_set_mqtt_state(const char *state_text) {
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }
    esp_err_t ret = set_chip_state_locked(s_mqtt_chip, s_mqtt_chip_label, state_text);
    lvgl_port_unlock();
    return ret;
}

esp_err_t ui_set_ha_state(const char *state_text) {
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }
    esp_err_t ret = set_chip_state_locked(s_ha_chip, s_ha_chip_label, state_text);
    lvgl_port_unlock();
    return ret;
}

esp_err_t ui_set_button_label(size_t index, const char *label_text) {
    if (index >= UI_MAX_DYNAMIC_BUTTONS || label_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    lv_obj_t *button = s_dynamic_buttons[index];
    lv_obj_t *label = s_dynamic_button_labels[index];
    if (button == NULL || label == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (label_text[0] == '\0') {
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(label, label_text);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
    update_footer_layout_locked();

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_button_state(size_t index, const char *state_text) {
    if (index >= UI_MAX_DYNAMIC_BUTTONS || state_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    lv_obj_t *button = s_dynamic_buttons[index];
    lv_obj_t *label = s_dynamic_button_labels[index];
    lv_obj_t *track = s_dynamic_button_switches[index];
    if (button == NULL || label == NULL || track == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bool stateful = string_equals_ci(state_text, "on") || string_equals_ci(state_text, "off") ||
                    string_equals_ci(state_text, "true") || string_equals_ci(state_text, "false");
    bool active = string_equals_ci(state_text, "on") || string_equals_ci(state_text, "true");
    if (!stateful) {
        lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_right(label, 0, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_AUTO, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_center(label);
    } else {
        lv_obj_clear_flag(track, LV_OBJ_FLAG_HIDDEN);
        set_toggle_visual(track, active);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_pad_right(label, 50, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_measurement_chip(size_t index, const char *chip_text) {
    if (index >= UI_MAX_MEASUREMENT_CHIPS || chip_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    lv_obj_t *label = s_measurement_chip_labels[index];
    if (label == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (chip_text[0] == '\0') {
        lv_obj_add_flag(s_measurement_chips[index], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(label, chip_text);
        lv_obj_clear_flag(s_measurement_chips[index], LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_measurement_chip_color(size_t index, const char *color_text) {
    if (index >= UI_MAX_MEASUREMENT_CHIPS || color_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }

    esp_err_t ret = set_measurement_chip_color_locked(index, color_text);
    lvgl_port_unlock();
    return ret;
}
