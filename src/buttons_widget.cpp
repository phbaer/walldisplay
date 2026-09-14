#include "walldisplay/buttons_widget.h"
#include "walldisplay/panel_component.hpp"
#include "walldisplay/ui_theme.h"
#include "walldisplay/ui_actions.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>
#include <new>
struct buttons_widget : PanelComponent {
    lv_obj_t *root = nullptr;
lv_obj_t *s_buttons_title{};
lv_obj_t *s_grid_buttons[6]{};
lv_obj_t *s_grid_labels[6]{};
int s_grid_slots[6]{};
static void grid_button_event_cb(lv_event_t *event) {
    int slot = *(int *)lv_event_get_user_data(event);
    if (ui_actions_emit({UI_ACTION_GRID, slot}) != ESP_OK)
        ESP_LOGW("buttons", "Could not queue grid button press");
}

lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    style_panel(page, UI_COLOR_SURFACE_ALT, 14);
    lv_obj_set_size(page, UI_MAIN_CONTENT_WIDTH, UI_MAIN_COMPACT_HEIGHT);
    lv_obj_set_style_pad_all(page, 16, 0);
    s_buttons_title = lv_label_create(page);
    lv_label_set_text(s_buttons_title, "Buttons");
    lv_obj_set_style_text_font(s_buttons_title, font_ui_16(), 0);
    lv_obj_set_style_text_color(s_buttons_title, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 6; ++i) {
        s_grid_slots[i] = i;
        lv_obj_t *button = lv_btn_create(page);
        style_button(button);
        lv_obj_set_size(button, 174, 56);
        lv_obj_set_pos(button, (i % 2) * 184, 40 + (i / 2) * 62);
        lv_obj_add_event_cb(button, grid_button_event_cb, LV_EVENT_CLICKED, &s_grid_slots[i]);
        s_grid_buttons[i] = button;
        s_grid_labels[i] = lv_label_create(button);
        lv_obj_set_style_text_font(s_grid_labels[i], font_ui_16(), 0);
        lv_obj_set_style_text_color(s_grid_labels[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_width(s_grid_labels[i], 154);
        lv_label_set_long_mode(s_grid_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_grid_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_grid_labels[i]);
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
    root = page;
    return page;
}

esp_err_t update_slot(size_t index, const char *json) {
    if (index >= 6 || !json) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(json);
    cJSON *label = cJSON_GetObjectItemCaseSensitive(root, "label");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (!cJSON_IsString(label) || strlen(label->valuestring) > 96 || !cJSON_IsString(state)) {
        cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(0)) { cJSON_Delete(root); return ESP_ERR_TIMEOUT; }
    lv_obj_t *button = s_grid_buttons[index];
    lv_label_set_text(s_grid_labels[index], label->valuestring);
    if (label->valuestring[0]) lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(state->valuestring, "on") == 0) lv_obj_add_state(button, LV_STATE_CHECKED);
    else lv_obj_clear_state(button, LV_STATE_CHECKED);
    if (strcmp(state->valuestring, "unavailable") == 0 || strcmp(state->valuestring, "unknown") == 0)
        lv_obj_add_state(button, LV_STATE_DISABLED);
    else lv_obj_clear_state(button, LV_STATE_DISABLED);
    lvgl_port_unlock();
    cJSON_Delete(root);
    return ESP_OK;
}


    esp_err_t update(const char *json) override {
        cJSON *root = cJSON_Parse(json);
        cJSON *slot = cJSON_GetObjectItemCaseSensitive(root, "slot");
        int index = cJSON_IsNumber(slot) ? slot->valueint - 1 : -1;
        bool valid = index >= 0 && index < 6 && slot->valuedouble == index + 1;
        cJSON_Delete(root);
        return valid ? update_slot(index, json) : ESP_ERR_INVALID_ARG;
    }
    void set_visible(bool visible) override {
        if (visible) lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
};
extern "C" buttons_widget_t *buttons_widget_create(lv_obj_t *parent) {
    auto *widget = new (std::nothrow) buttons_widget{};
    if (widget) widget->create(parent);
    return widget;
}
extern "C" lv_obj_t *buttons_widget_root(buttons_widget_t *widget) { return widget->root; }
extern "C" esp_err_t buttons_widget_update(buttons_widget_t *widget, size_t slot, const char *text) { return widget->update_slot(slot, text); }
extern "C" void buttons_widget_set_title(buttons_widget_t *widget, const char *title) {
    lv_obj_set_width(widget->s_buttons_title, 370);
    lv_label_set_long_mode(widget->s_buttons_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->s_buttons_title, title);
}
