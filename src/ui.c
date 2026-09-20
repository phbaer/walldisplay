#include "walldisplay/ui.h"

#include "walldisplay/app_config.h"
#include "walldisplay/display_dimming.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "src/libs/qrcode/lv_qrcode.h"
#include "cJSON.h"
#include "walldisplay/ui_actions.h"
#include "walldisplay/ui_theme.h"
#include "walldisplay/weather_widget.h"
#include "walldisplay/buttons_widget.h"
#include "walldisplay/media_widget.h"
#include "walldisplay/ui_assets.h"
#include "walldisplay/ui_font_noto_16.h"
#include "walldisplay/ui_font_temperature_28_bold.h"
#include "walldisplay/ui_font_weather_symbols_14.h"

#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "walldisplay/wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define UI_MAX_DYNAMIC_BUTTONS 5
#define UI_MAX_MEASUREMENT_CHIPS 4
#define UI_MAX_MEDIA_FAVORITES 5

#define UI_SCREEN_MARGIN 10
#define UI_HEADER_HEIGHT 112
#define UI_MAIN_HEIGHT 242
#define UI_FOOTER_COMPACT_HEIGHT 76
#define UI_MAIN_FULL_HEIGHT 340
#define UI_GAP 10
#define UI_STATUS_CHIP_WIDTH 36
#define UI_MEASUREMENT_CHIP_WIDTH 82
#define UI_STATUS_CHIP_ROW_WIDTH (3 * UI_STATUS_CHIP_WIDTH)
#define UI_MEASUREMENT_CHIP_ROW_WIDTH (UI_MAX_MEASUREMENT_CHIPS * UI_MEASUREMENT_CHIP_WIDTH)


static const char *TAG = "ui";
static lv_obj_t *s_title_label;
static lv_obj_t *s_media_play_label;
static lv_obj_t *s_media_volume_slider;
static media_widget_t *s_media_widget;
static lv_obj_t *s_main_area;
static lv_obj_t *s_weather_page;
static weather_widget_t *s_weather_widget;
static buttons_widget_t *s_buttons_widget;
static lv_obj_t *s_media_page;
static lv_obj_t *s_about_page;
static lv_obj_t *s_pages[PANEL_PAGE_COUNT];
static lv_obj_t *s_page_nav;
static lv_obj_t *s_page_nav_buttons[PANEL_MAX_PAGES];
static lv_obj_t *s_page_nav_labels[PANEL_MAX_PAGES];
static panel_page_id_t s_page_nav_targets[PANEL_MAX_PAGES];
static panel_layout_t s_layout;
static panel_page_id_t s_current_page;
static char s_panel_name[128];
static lv_obj_t *s_footer;
static lv_obj_t *s_dynamic_row;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_about_value_label;
static lv_obj_t *s_about_qr;
static bool s_about_qr_ap_active;
static lv_timer_t *s_about_refresh_timer;
static lv_obj_t *s_update_screen;
static lv_obj_t *s_update_detail_label;
static lv_obj_t *s_update_progress_bar;
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
static void page_nav_event_cb(lv_event_t *event);

/* Static Noto Sans renders all UI text without runtime glyph allocation. */
static const lv_font_t *font_ui_20(void) { return &ui_font_noto_16; }
static const lv_font_t *font_time(void) { return &lv_font_montserrat_28; }

/* LVGL's private-use LV_SYMBOL_* glyphs are supplied by Montserrat. */
static const lv_font_t *font_symbols_14(void) { return &lv_font_montserrat_14; }

static const char *const s_page_nav_symbols[PANEL_PAGE_COUNT] = {
    LV_SYMBOL_TINT, LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS,
};

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

static esp_err_t publish_dynamic_button_action(size_t index) {
    return ui_actions_emit((ui_action_t){UI_ACTION_FOOTER, (int)index});
}
static esp_err_t publish_media_command(const char *command) {
    static const char *const names[] = {"previous", "play_pause", "next", "power_off", "volume_down", "volume_up"};
    for (size_t i = 0; i < 6; ++i) if (strcmp(command, names[i]) == 0)
        return ui_actions_emit((ui_action_t){UI_ACTION_PREVIOUS + i, 0});
    return ESP_ERR_INVALID_ARG;
}
static esp_err_t publish_media_volume(int volume_percent) {
    return ui_actions_emit((ui_action_t){UI_ACTION_VOLUME, volume_percent});
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

static lv_obj_t *create_main_page(lv_obj_t *parent, const char *title, const char *placeholder,
                                  lv_obj_t **out_value_label) {
    lv_obj_t *page = lv_obj_create(parent);
    style_panel(page, UI_COLOR_SURFACE_ALT, 14);
    lv_obj_set_size(page, UI_MAIN_CONTENT_WIDTH, UI_MAIN_HEIGHT);
    lv_obj_set_style_pad_all(page, 16, 0);

    lv_obj_t *title_label = lv_label_create(page);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_ui_16(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xA4ACB8), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *value = lv_label_create(page);
    lv_label_set_text(value, placeholder);
    lv_obj_set_width(value, UI_MAIN_CONTENT_WIDTH - 36);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, font_ui_20(), 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xECECEC), 0);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 0, 42);

    if (out_value_label != NULL) {
        *out_value_label = value;
    }
    return page;
}

static const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

static void format_ipv6_address(const esp_ip6_addr_t *address, char *output, size_t output_size) {
    if (address == NULL || output == NULL || output_size == 0) return;
    snprintf(output, output_size, IPV6STR, IPV62STR(*address));
}

static const char *configuration_state(const char *value, const char *placeholder) {
    return value != NULL && value[0] != '\0' &&
                   (placeholder == NULL || strcmp(value, placeholder) != 0)
               ? "configured"
               : "not configured";
}

static void refresh_about_page(void) {
    if (s_about_value_label == NULL) return;

    char mac_text[18] = "unknown";
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    char ipv4_text[16] = "unavailable";
    char ipv6_global_text[40] = "unavailable";
    char ipv6_linklocal_text[40] = "unavailable";
    char wifi_text[64] = "unavailable";
    char ap_text[256] = "";
    char hostname_text[64] = "unavailable";
    const app_config_t *config = app_config_get();
    const bool ap_active = wifi_manager_ap_active();
    if (ap_active) {
        char ap_ssid[33], ap_password[65];
        if (wifi_manager_get_ap_credentials(ap_ssid, sizeof(ap_ssid), ap_password, sizeof(ap_password)) == ESP_OK) {
            snprintf(ap_text, sizeof(ap_text), "SETUP AP\nSSID: %s\nPassword: %s\nPortal: 192.168.4.1", ap_ssid, ap_password);
            if (s_about_qr != NULL && !s_about_qr_ap_active) {
                char qr_data[140];
                snprintf(qr_data, sizeof(qr_data), "WIFI:T:WPA;S:%s;P:%s;;", ap_ssid, ap_password);
                lv_qrcode_update(s_about_qr, qr_data, strlen(qr_data));
                lv_obj_clear_flag(s_about_qr, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else if (s_about_qr != NULL && s_about_qr_ap_active) {
        lv_obj_add_flag(s_about_qr, LV_OBJ_FLAG_HIDDEN);
    }
    s_about_qr_ap_active = ap_active;
    const char *wifi_credentials = configuration_state(config->wifi_ssid, "YOUR_WIFI_SSID");
    const char *mqtt_credentials = configuration_state(config->mqtt_uri, "mqtts://YOUR_MQTT_BROKER");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, ipv4_text, sizeof(ipv4_text));
        }

        const char *hostname = NULL;
        if (esp_netif_get_hostname(netif, &hostname) == ESP_OK && hostname != NULL && hostname[0] != '\0') {
            snprintf(hostname_text, sizeof(hostname_text), "%s", hostname);
        }

        esp_ip6_addr_t ipv6 = {0};
        if (esp_netif_get_ip6_global(netif, &ipv6) == ESP_OK) {
            format_ipv6_address(&ipv6, ipv6_global_text, sizeof(ipv6_global_text));
        }
        if (esp_netif_get_ip6_linklocal(netif, &ipv6) == ESP_OK) {
            format_ipv6_address(&ipv6, ipv6_linklocal_text, sizeof(ipv6_linklocal_text));
        }

        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            snprintf(wifi_text, sizeof(wifi_text), "%s (%d dBm, ch %u)",
                     (const char *) ap_info.ssid, ap_info.rssi, ap_info.primary);
        }
    }

    const uint64_t uptime_s = (uint64_t) (esp_timer_get_time() / 1000000LL);
    const unsigned days = (unsigned) (uptime_s / 86400U);
    const unsigned hours = (unsigned) ((uptime_s / 3600U) % 24U);
    const unsigned minutes = (unsigned) ((uptime_s / 60U) % 60U);
    const unsigned seconds = (unsigned) (uptime_s % 60U);
    char about_text[768];
    snprintf(about_text, sizeof(about_text),
             "SYSTEM\nFirmware: %s\nMQTT contract: %s\nModel: %s\nUptime: %ud %02u:%02u:%02u\nReset: %s\n\nNETWORK\nHostname: %s\nIPv4: %s\nIPv6: %s\nIPv6 LL: %s\n"
             "MAC: %s\nWi-Fi: %s\nWi-Fi credentials: %s\nMQTT: %s%s%s",
             APP_FW_VERSION, APP_CONTRACT_VERSION, APP_DEVICE_MODEL,
             days, hours, minutes, seconds,
             reset_reason_name(esp_reset_reason()), hostname_text, ipv4_text, ipv6_global_text, ipv6_linklocal_text,
             mac_text, wifi_text, wifi_credentials, mqtt_credentials, ap_text[0] ? "\n\n" : "", ap_text);
    lv_label_set_text(s_about_value_label, about_text);
}

static void about_refresh_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_about_page();
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
    if (ui_actions_emit((ui_action_t){UI_ACTION_FAVORITE, *slot}) != ESP_OK) ESP_LOGW(TAG, "Media favourite publish failed");
}

static const char *media_favorite_symbol(const char *icon_name) {
    if (icon_name == NULL || string_equals_ci(icon_name, "none")) return "";
    if (string_equals_ci(icon_name, "radio") || string_equals_ci(icon_name, "music")) return LV_SYMBOL_AUDIO;
    if (string_equals_ci(icon_name, "album")) return LV_SYMBOL_IMAGE;
    if (string_equals_ci(icon_name, "playlist")) return LV_SYMBOL_LIST;
    if (string_equals_ci(icon_name, "podcast")) return LV_SYMBOL_LOOP;
    return "";
}

static void show_page_locked(panel_page_id_t id) {
    for (size_t i = 0; i < PANEL_PAGE_COUNT; ++i) {
        if (i == (size_t)id) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_current_page = id;
    lv_label_set_text_fmt(s_title_label, "%s%s%s", s_panel_name,
                          s_panel_name[0] ? " - " : "", s_layout.titles[id]);
    for (size_t i = 0; i < PANEL_PAGE_COUNT; ++i) {
        if (s_page_nav_buttons[i] == NULL) continue;
        if (s_page_nav_targets[i] == id) lv_obj_add_state(s_page_nav_buttons[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(s_page_nav_buttons[i], LV_STATE_CHECKED);
    }
}

static void page_nav_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const panel_page_id_t *target = lv_event_get_user_data(event);
    if (target != NULL && panel_layout_contains(&s_layout, *target)) show_page_locked(*target);
}

esp_err_t ui_show_page(const char *page_name) {
    panel_page_id_t id;
    if (!panel_page_parse(page_name, &id)) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(pdMS_TO_TICKS(1000))) return ESP_ERR_TIMEOUT;
    if (!panel_layout_contains(&s_layout, id)) { lvgl_port_unlock(); return ESP_ERR_INVALID_ARG; }
    show_page_locked(id);
    lvgl_port_unlock();
    return ESP_OK;
}

static void apply_layout_locked(void) {
    s_layout = app_config_get()->layout;
    /* Keep the About page available for setup diagnostics even when a custom
     * layout omits it. It is appended after all user-selected pages. */
    if (!panel_layout_contains(&s_layout, PANEL_PAGE_ABOUT) && s_layout.count < PANEL_MAX_PAGES)
        s_layout.order[s_layout.count++] = PANEL_PAGE_ABOUT;
    if (s_page_nav != NULL) {
        if (s_layout.count <= 1) lv_obj_add_flag(s_page_nav, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_page_nav, LV_OBJ_FLAG_HIDDEN);
        for (size_t i = 0; i < PANEL_MAX_PAGES; ++i) {
            if (i < s_layout.count) {
                s_page_nav_targets[i] = s_layout.order[i];
                lv_label_set_text(s_page_nav_labels[i], s_page_nav_symbols[s_page_nav_targets[i]]);
                lv_obj_clear_flag(s_page_nav_buttons[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_page_nav_buttons[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    weather_widget_set_title(s_weather_widget, s_layout.titles[PANEL_PAGE_WEATHER]);
    buttons_widget_set_title(s_buttons_widget, s_layout.titles[PANEL_PAGE_BUTTONS]);
    show_page_locked(s_layout.default_page);
}

esp_err_t ui_apply_page_layout(void) {
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    char current[PANEL_LAYOUT_JSON_SIZE], next[PANEL_LAYOUT_JSON_SIZE];
    if (!panel_layout_json(&s_layout, current, sizeof(current)) ||
        !panel_layout_json(&app_config_get()->layout, next, sizeof(next)) || strcmp(current, next) != 0)
        apply_layout_locked();
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_grid_button(size_t index, const char *json) { return buttons_widget_update(s_buttons_widget, index, json); }

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
    for (size_t i = 0; i < PANEL_PAGE_COUNT; ++i) lv_obj_set_height(s_pages[i], main_height);
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
    lv_obj_set_style_clip_corner(status_control, false, 0);
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

    s_weather_widget = weather_widget_create(s_main_area);
    if (!s_weather_widget) { lvgl_port_unlock(); return ESP_ERR_NO_MEM; }
    s_weather_page = weather_widget_root(s_weather_widget);
    s_media_page = create_main_page(s_main_area, "", "", NULL);
    s_media_widget = media_widget_create(s_media_page);
    if (s_media_widget == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_obj_t *media_controls = lv_obj_create(s_media_page);
    lv_obj_remove_style_all(media_controls);
    lv_obj_set_size(media_controls, UI_MAIN_CONTENT_WIDTH - 36, 54);
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
    lv_obj_set_size(volume_rocker, 172, 52);
    lv_obj_set_style_pad_all(volume_rocker, 4, 0);
    lv_obj_set_style_clip_corner(volume_rocker, false, 0);
    lv_obj_t *volume_down_button = create_media_button(volume_rocker, LV_SYMBOL_MINUS, 40, 44, "volume_down", media_control_event_cb, true);
    style_volume_rocker_button(volume_down_button);
    lv_obj_align(volume_down_button, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_event_cb(volume_down_button, media_control_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, "volume_down");

    s_media_volume_slider = lv_slider_create(volume_rocker);
    lv_slider_set_range(s_media_volume_slider, 0, 100);
    lv_slider_set_value(s_media_volume_slider, 50, LV_ANIM_OFF);
    lv_obj_set_size(s_media_volume_slider, 82, 14);
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

    lv_obj_t *power_button = create_media_button(s_media_page, LV_SYMBOL_POWER, 38, 34, "power_off", media_control_event_cb, true);
    lv_obj_align(power_button, LV_ALIGN_TOP_RIGHT, -8, 8);

    lv_obj_t *media_favorites = lv_obj_create(s_media_page);
    lv_obj_remove_style_all(media_favorites);
    lv_obj_set_size(media_favorites, UI_MAIN_CONTENT_WIDTH - 36, 34);
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
    s_pages[PANEL_PAGE_WEATHER] = s_weather_page;
    s_pages[PANEL_PAGE_MEDIA] = s_media_page;
    s_buttons_widget = buttons_widget_create(s_main_area);
    if (!s_buttons_widget) { lvgl_port_unlock(); return ESP_ERR_NO_MEM; }
    s_pages[PANEL_PAGE_BUTTONS] = buttons_widget_root(s_buttons_widget);
    s_about_page = create_main_page(s_main_area, "", "", &s_about_value_label);
    if (s_about_page == NULL) { lvgl_port_unlock(); return ESP_ERR_NO_MEM; }
    lv_obj_set_width(s_about_value_label, UI_MAIN_CONTENT_WIDTH - 24);
    lv_obj_set_style_text_font(s_about_value_label, font_ui_14(), 0);
    lv_obj_align(s_about_value_label, LV_ALIGN_TOP_LEFT, 0, 10);
    s_about_qr = lv_qrcode_create(s_about_page);
    if (s_about_qr != NULL) {
        lv_qrcode_set_size(s_about_qr, 128);
        lv_obj_align(s_about_qr, LV_ALIGN_TOP_RIGHT, -8, 8);
        lv_obj_add_flag(s_about_qr, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_about_page();
    s_about_refresh_timer = lv_timer_create(about_refresh_timer_cb, 5000, NULL);
    if (s_about_refresh_timer == NULL) { lvgl_port_unlock(); return ESP_ERR_NO_MEM; }
    s_pages[PANEL_PAGE_ABOUT] = s_about_page;

    s_page_nav = lv_obj_create(s_main_area);
    style_panel(s_page_nav, UI_COLOR_SURFACE, 10);
    lv_obj_set_size(s_page_nav, UI_PAGE_NAV_WIDTH, UI_PAGE_NAV_HEIGHT);
    lv_obj_set_style_pad_all(s_page_nav, 4, 0);
    lv_obj_set_style_pad_row(s_page_nav, 4, 0);
    lv_obj_set_layout(s_page_nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_page_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page_nav, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_page_nav, LV_ALIGN_RIGHT_MID, 0, 0);
    for (size_t i = 0; i < PANEL_MAX_PAGES; ++i) {
        s_page_nav_targets[i] = PANEL_PAGE_WEATHER;
        lv_obj_t *button = lv_btn_create(s_page_nav);
        style_button(button);
        lv_obj_set_size(button, 34, 34);
        lv_obj_add_event_cb(button, page_nav_event_cb, LV_EVENT_CLICKED, &s_page_nav_targets[i]);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, font_symbols_14(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_label_set_text(label, "-");
        lv_obj_center(label);
        s_page_nav_buttons[i] = button;
        s_page_nav_labels[i] = label;
    }
    apply_layout_locked();

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

    /* Keep OTA visually quiet: this opaque screen is the last child, so
     * retained MQTT updates underneath it cannot produce visible redraws. */
    s_update_screen = lv_obj_create(screen);
    if (s_update_screen == NULL) { lvgl_port_unlock(); return ESP_ERR_NO_MEM; }
    lv_obj_remove_style_all(s_update_screen);
    lv_obj_set_size(s_update_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_update_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_update_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_update_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_update_screen, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *update_title = lv_label_create(s_update_screen);
    lv_label_set_text(update_title, "Firmware update");
    /* The temperature font intentionally contains only digits and a degree
     * sign. Using it for this text renders unsupported letters as boxes. */
    lv_obj_set_style_text_font(update_title, font_ui_16(), 0);
    lv_obj_set_style_text_color(update_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(update_title, LV_ALIGN_CENTER, 0, -20);

    s_update_detail_label = lv_label_create(s_update_screen);
    lv_label_set_text(s_update_detail_label, "Please wait...");
    lv_obj_set_style_text_font(s_update_detail_label, font_ui_16(), 0);
    lv_obj_set_style_text_color(s_update_detail_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_update_detail_label, LV_ALIGN_CENTER, 0, 24);

    s_update_progress_bar = lv_bar_create(s_update_screen);
    lv_obj_set_size(s_update_progress_bar, 240, 10);
    lv_obj_align(s_update_progress_bar, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(s_update_progress_bar, lv_color_hex(0x263241), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_update_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_update_progress_bar, lv_color_hex(0x4EA1FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_update_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(s_update_progress_bar, 0, 100);
    lv_bar_set_value(s_update_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_update_screen, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI scaffold created");
    return ESP_OK;
}

esp_err_t ui_set_connection_status(const char *status_text) {
    /* MQTT availability remains functional, but no status text is rendered. */
    return status_text == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t ui_set_title_text(const char *title_text) {
    if (!title_text) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    strlcpy(s_panel_name, title_text, sizeof(s_panel_name));
    show_page_locked(s_current_page);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_weather_text(const char *text) { return weather_widget_update(s_weather_widget, text); }

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

esp_err_t ui_show_update_screen(void) {
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    if (s_update_screen == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    lv_obj_clear_flag(s_update_screen, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_update_detail_label, "Preparing update...");
    lv_bar_set_value(s_update_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_move_foreground(s_update_screen);
    /* Render the opaque overlay immediately, before OTA networking starts.
     * Otherwise the RGB DMA can show an intermediate page frame. */
    /* Direct RGB mode has two panel-owned frame buffers. Render the opaque
     * overlay into both so a buffer swap cannot expose the old header (or an
     * unsupported glyph from it) during the transfer. */
    for (int frame = 0; frame < 2; ++frame) {
        lv_obj_invalidate(s_update_screen);
        lv_refr_now(lv_display_get_default());
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_update_progress(uint8_t percent) {
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    if (s_update_detail_label == NULL || s_update_progress_bar == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    char detail[32];
    snprintf(detail, sizeof(detail), "Downloading firmware... %u%%", (unsigned) percent);
    lv_label_set_text(s_update_detail_label, detail);
    lv_bar_set_value(s_update_progress_bar, percent, LV_ANIM_OFF);
    lv_obj_invalidate(s_update_screen);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_hide_update_screen(void) {
    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
    if (s_update_screen == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    lv_obj_add_flag(s_update_screen, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    return ESP_OK;
}
