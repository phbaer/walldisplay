#include "walldisplay/media_widget.h"
#include "walldisplay/panel_component.hpp"

#include "cJSON.h"
#include "esp_lvgl_port.h"
#include "walldisplay/ui_font_noto_16.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kControlColor = 0x1A1F26;
constexpr uint32_t kBorderColor = 0x252B33;
constexpr uint32_t kTextColor = 0xF2F2F2;
constexpr uint32_t kMutedTextColor = 0xA4ACB8;

void normalize_text(char *text) {
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        const unsigned char first = static_cast<unsigned char>(*read);
        unsigned codepoint = first;
        size_t length = 1;
        if ((first & 0xe0) == 0xc0 && (static_cast<unsigned char>(read[1]) & 0xc0) == 0x80) {
            codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(read[1]) & 0x3f); length = 2;
        } else if ((first & 0xf0) == 0xe0 && (static_cast<unsigned char>(read[1]) & 0xc0) == 0x80 && (static_cast<unsigned char>(read[2]) & 0xc0) == 0x80) {
            codepoint = ((first & 0x0f) << 12) | ((static_cast<unsigned char>(read[1]) & 0x3f) << 6) | (static_cast<unsigned char>(read[2]) & 0x3f); length = 3;
        } else if ((first & 0xf8) == 0xf0 && (static_cast<unsigned char>(read[1]) & 0xc0) == 0x80 && (static_cast<unsigned char>(read[2]) & 0xc0) == 0x80 && (static_cast<unsigned char>(read[3]) & 0xc0) == 0x80) {
            codepoint = 0x110000; length = 4;
        }
        if (codepoint == '\n' || (codepoint >= 0x20 && codepoint <= 0x052f) ||
            (codepoint >= 0x2000 && codepoint <= 0x2027) || (codepoint >= 0x2030 && codepoint <= 0x205e) ||
            codepoint == 0x20ac || codepoint == 0x2122) {
            for (size_t i = 0; i < length; ++i) *write++ = read[i];
        } else if (codepoint == 0x2018 || codepoint == 0x2019) *write++ = '\'';
        else if (codepoint == 0x201c || codepoint == 0x201d) *write++ = '"';
        else if (codepoint == 0x2013 || codepoint == 0x2014 || codepoint == 0x2212) *write++ = '-';
        else if (codepoint == 0x2026) { *write++ = '.'; *write++ = '.'; *write++ = '.'; }
        else *write++ = '?';
        read += length;
    }
    *write = '\0';
}

class MediaWidget final : public PanelComponent {
public:
    esp_err_t create(lv_obj_t *page) {
        if (page == nullptr) return ESP_ERR_INVALID_ARG;
        page_ = page;

        value_label_ = lv_label_create(page_);
        lv_label_set_text(value_label_, "Waiting for media...");
        lv_obj_set_width(value_label_, 226);
        lv_label_set_long_mode(value_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(value_label_, &ui_font_noto_16, 0);
        lv_obj_set_style_text_color(value_label_, lv_color_hex(kTextColor), 0);
        lv_obj_align(value_label_, LV_ALIGN_TOP_LEFT, 146, 4);

        placeholder_ = lv_obj_create(page_);
        lv_obj_remove_style_all(placeholder_);
        lv_obj_set_size(placeholder_, 136, 136);
        lv_obj_set_style_bg_color(placeholder_, lv_color_hex(kControlColor), 0);
        lv_obj_set_style_bg_opa(placeholder_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(placeholder_, 1, 0);
        lv_obj_set_style_border_color(placeholder_, lv_color_hex(kBorderColor), 0);
        lv_obj_set_style_radius(placeholder_, 10, 0);
        lv_obj_align(placeholder_, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *symbol = lv_label_create(placeholder_);
        lv_label_set_text(symbol, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(symbol, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(kMutedTextColor), 0);
        lv_obj_center(symbol);

        artwork_ = lv_image_create(page_);
        lv_obj_set_size(artwork_, 136, 136);
        lv_obj_align(artwork_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_add_flag(artwork_, LV_OBJ_FLAG_HIDDEN);
        return ESP_OK;
    }

    void set_play_label(lv_obj_t *play_label) { play_label_ = play_label; }

    esp_err_t update(const char *payload) override {
        if (payload == nullptr || value_label_ == nullptr) return ESP_ERR_INVALID_ARG;
        cJSON *root = cJSON_Parse(payload);
        if (!cJSON_IsObject(root)) {
            const esp_err_t result = set_text(payload, false, false);
            cJSON_Delete(root);
            return result;
        }

        const cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
        const cJSON *artist = cJSON_GetObjectItemCaseSensitive(root, "artist");
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
        char text[384];
        std::snprintf(text, sizeof(text), "%s%s%s%s%s%s%s",
                      cJSON_IsString(title) ? title->valuestring : "Media idle",
                      cJSON_IsString(artist) ? "\n" : "", cJSON_IsString(artist) ? artist->valuestring : "",
                      cJSON_IsString(source) && source->valuestring[0] ? "\n" : "",
                      cJSON_IsString(source) ? source->valuestring : "",
                      cJSON_IsString(state) ? "\n" : "", cJSON_IsString(state) ? state->valuestring : "");
        normalize_text(text);
        const bool playing = cJSON_IsString(state) && std::strcmp(state->valuestring, "playing") == 0;
        cJSON_Delete(root);
        return set_text(text, playing, true);
    }

    void set_visible(bool visible) override {
        if (page_ == nullptr) return;
        if (visible) lv_obj_clear_flag(page_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page_, LV_OBJ_FLAG_HIDDEN);
    }

    esp_err_t set_artwork(const uint16_t *pixels, size_t width, size_t height) {
        if (artwork_ == nullptr || placeholder_ == nullptr) return ESP_ERR_INVALID_STATE;
        if (pixels == nullptr || width == 0 || height == 0 || width > UINT16_MAX || height > UINT16_MAX) {
            lv_obj_add_flag(artwork_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
            return pixels == nullptr ? ESP_OK : ESP_ERR_INVALID_ARG;
        }
        artwork_dsc_ = {};
        artwork_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        artwork_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        artwork_dsc_.header.w = static_cast<uint16_t>(width);
        artwork_dsc_.header.h = static_cast<uint16_t>(height);
        artwork_dsc_.header.stride = static_cast<uint32_t>(width * 2);
        artwork_dsc_.data_size = static_cast<uint32_t>(width * height * 2);
        artwork_dsc_.data = reinterpret_cast<const uint8_t *>(pixels);
        lv_image_set_src(artwork_, &artwork_dsc_);
        lv_obj_clear_flag(artwork_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
        return ESP_OK;
    }

private:
    esp_err_t set_text(const char *text, bool playing, bool update_play_icon) {
        if (!lvgl_port_lock(0)) return ESP_FAIL;
        lv_label_set_text(value_label_, text);
        if (update_play_icon && play_label_ != nullptr) lv_label_set_text(play_label_, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lvgl_port_unlock();
        return ESP_OK;
    }

    lv_obj_t *page_ = nullptr;
    lv_obj_t *value_label_ = nullptr;
    lv_obj_t *play_label_ = nullptr;
    lv_obj_t *artwork_ = nullptr;
    lv_obj_t *placeholder_ = nullptr;
    lv_image_dsc_t artwork_dsc_{};
};

}  // namespace

struct media_widget {
    MediaWidget implementation;
};

extern "C" media_widget_t *media_widget_create(lv_obj_t *page) {
    auto *widget = new (std::nothrow) media_widget;
    if (widget == nullptr || widget->implementation.create(page) != ESP_OK) {
        delete widget;
        return nullptr;
    }
    return widget;
}

extern "C" void media_widget_destroy(media_widget_t *widget) {
    delete widget;
}

extern "C" void media_widget_set_play_label(media_widget_t *widget, lv_obj_t *play_label) {
    if (widget != nullptr) widget->implementation.set_play_label(play_label);
}

extern "C" esp_err_t media_widget_update(media_widget_t *widget, const char *payload) {
    return widget == nullptr ? ESP_ERR_INVALID_STATE : widget->implementation.update(payload);
}

extern "C" esp_err_t media_widget_set_artwork(media_widget_t *widget, const uint16_t *pixels, size_t width, size_t height) {
    if (widget == nullptr || !lvgl_port_lock(0)) return ESP_FAIL;
    const esp_err_t result = widget->implementation.set_artwork(pixels, width, height);
    lvgl_port_unlock();
    return result;
}
