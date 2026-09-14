#pragma once
#include "lvgl.h"
#include "ui_font_noto_16.h"
#include "ui_font_temperature_28_bold.h"
#include "ui_font_weather_symbols_14.h"
#define UI_CONTENT_WIDTH 460
#define UI_MAIN_COMPACT_HEIGHT 254
#define UI_COLOR_SURFACE 0x0C0D10
#define UI_COLOR_SURFACE_ALT 0x101317
#define UI_COLOR_CONTROL 0x1A1F26
#define UI_COLOR_CONTROL_PRESSED 0x2B323C
#define UI_COLOR_BORDER 0x252B33
#define UI_COLOR_TEXT 0xF2F2F2
#define UI_COLOR_TEXT_MUTED 0xA4ACB8
static inline const lv_font_t *font_ui_14(void) { return &ui_font_noto_16; }
static inline const lv_font_t *font_ui_16(void) { return &ui_font_noto_16; }
static inline const lv_font_t *font_ui_24(void) { return &ui_font_temperature_28_bold; }
static inline const lv_font_t *font_weather_symbols_14(void) { return &ui_font_weather_symbols_14; }
static inline void style_panel(lv_obj_t *object, uint32_t color, int radius) {
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(object, radius, 0);
}

static inline void style_button(lv_obj_t *button) {
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_CONTROL), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_CONTROL_PRESSED), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x234E68), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(button, LV_OPA_90, (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2D3540), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(0x425161), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
}
