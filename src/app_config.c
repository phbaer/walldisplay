#include "walldisplay/app_config.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "walldisplay/app_config_defaults.h"

#include <string.h>
#include <ctype.h>

static const char *TAG = "app_config";
static const char *APP_CONFIG_PARTITION = "appcfg";
static const char *APP_CONFIG_NAMESPACE = "appcfg";
static const char *RUNTIME_CONFIG_NAMESPACE = "runtimecfg";
static const app_config_t s_default_config = {
    .wifi_ssid = APPCFG_DEFAULT_WIFI_SSID,
    .wifi_password = APPCFG_DEFAULT_WIFI_PASSWORD,
    .mqtt_uri = APPCFG_DEFAULT_MQTT_URI,
    .mqtt_username = APPCFG_DEFAULT_MQTT_USERNAME,
    .mqtt_password = APPCFG_DEFAULT_MQTT_PASSWORD,
    .discovery_prefix = APPCFG_DEFAULT_DISCOVERY_PREFIX,
    .base_topic = APPCFG_DEFAULT_BASE_TOPIC,
    .display_name = "WallDisplay",
    .enable_discovery = APPCFG_DEFAULT_ENABLE_DISCOVERY,
    .mqtt_require_tls = APPCFG_MQTT_REQUIRE_TLS,
    .mqtt_ca_certificate = APPCFG_MQTT_CA_CERTIFICATE,
    .screenshot_token = APPCFG_SCREENSHOT_TOKEN,
    .default_page = APP_DEFAULT_PAGE_WEATHER,
};
static app_config_t s_app_config;
static bool s_initialized;

static void load_string_or_default(nvs_handle_t nvs_handle, const char *key, char *dest, size_t dest_size, const char *fallback) {
    size_t required_size = dest_size;

    if (nvs_get_str(nvs_handle, key, dest, &required_size) != ESP_OK) {
        strlcpy(dest, fallback, dest_size);
    }
}

esp_err_t app_config_init(void) {
    nvs_handle_t nvs_handle;

    if (s_initialized) {
        return ESP_OK;
    }

    s_app_config = s_default_config;
    panel_layout_defaults(&s_app_config.layout);
    if (!panel_layout_parse(APPCFG_DEFAULT_LAYOUT_JSON, &s_app_config.layout)) return ESP_ERR_INVALID_ARG;
    s_app_config.default_page = (app_default_page_t)s_app_config.layout.default_page;

    esp_err_t ret = nvs_flash_init_partition(APP_CONFIG_PARTITION);
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS partition '%s' not found, using built-in defaults", APP_CONFIG_PARTITION);
    } else {
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to initialize appcfg partition");

        ret = nvs_open_from_partition(APP_CONFIG_PARTITION, APP_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "NVS namespace '%s' not found in partition '%s', using built-in defaults", APP_CONFIG_NAMESPACE, APP_CONFIG_PARTITION);
        } else {
            ESP_RETURN_ON_ERROR(ret, TAG, "Failed to open appcfg namespace");

            load_string_or_default(nvs_handle, "wifi_ssid", s_app_config.wifi_ssid, sizeof(s_app_config.wifi_ssid), s_default_config.wifi_ssid);
            load_string_or_default(nvs_handle, "wifi_pass", s_app_config.wifi_password, sizeof(s_app_config.wifi_password), s_default_config.wifi_password);
            load_string_or_default(nvs_handle, "mqtt_uri", s_app_config.mqtt_uri, sizeof(s_app_config.mqtt_uri), s_default_config.mqtt_uri);
            load_string_or_default(nvs_handle, "mqtt_user", s_app_config.mqtt_username, sizeof(s_app_config.mqtt_username), s_default_config.mqtt_username);
            load_string_or_default(nvs_handle, "mqtt_pass", s_app_config.mqtt_password, sizeof(s_app_config.mqtt_password), s_default_config.mqtt_password);
            load_string_or_default(nvs_handle, "disc_pref", s_app_config.discovery_prefix, sizeof(s_app_config.discovery_prefix), s_default_config.discovery_prefix);
            load_string_or_default(nvs_handle, "base_topic", s_app_config.base_topic, sizeof(s_app_config.base_topic), s_default_config.base_topic);
            load_string_or_default(nvs_handle, "display_name", s_app_config.display_name, sizeof(s_app_config.display_name), s_default_config.display_name);

            uint8_t enable_discovery = s_default_config.enable_discovery;
            if (nvs_get_u8(nvs_handle, "discovery", &enable_discovery) == ESP_OK) {
                s_app_config.enable_discovery = enable_discovery != 0;
            }

            nvs_close(nvs_handle);
        }
    }

    ret = nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        size_t required_size = sizeof(s_app_config.base_topic);
        nvs_get_str(nvs_handle, "base_topic", s_app_config.base_topic, &required_size);
        required_size = sizeof(s_app_config.display_name);
        nvs_get_str(nvs_handle, "display_name", s_app_config.display_name, &required_size);
        uint8_t default_page;
        if (nvs_get_u8(nvs_handle, "default_page", &default_page) == ESP_OK && default_page < PANEL_PAGE_COUNT &&
            panel_layout_contains(&s_app_config.layout, (panel_page_id_t)default_page)) {
            s_app_config.default_page = (app_default_page_t)default_page;
        }
        char layout_json[PANEL_LAYOUT_JSON_SIZE];
        size_t layout_size = sizeof(layout_json);
        s_app_config.layout.default_page = (panel_page_id_t)s_app_config.default_page;
        if (nvs_get_str(nvs_handle, "pages", layout_json, &layout_size) == ESP_OK &&
            panel_layout_parse(layout_json, &s_app_config.layout)) {
            s_app_config.default_page = (app_default_page_t)s_app_config.layout.default_page;
        }
        nvs_close(nvs_handle);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to open runtime config");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "App config loaded");
    return ESP_OK;
}

const app_config_t *app_config_get(void) {
    if (!s_initialized) {
        s_app_config = s_default_config;
    }
    return &s_app_config;
}

esp_err_t app_config_set_base_topic(const char *base_topic) {
    nvs_handle_t nvs_handle;
    size_t length;

    ESP_RETURN_ON_FALSE(base_topic != NULL, ESP_ERR_INVALID_ARG, TAG, "Base topic is null");
    length = strlen(base_topic);
    ESP_RETURN_ON_FALSE(length > 0 && length <= APP_TOPIC_MAX_LEN, ESP_ERR_INVALID_ARG, TAG, "Invalid base topic length");
    for (size_t i = 0; i < length; ++i) {
        const unsigned char character = (unsigned char) base_topic[i];
        ESP_RETURN_ON_FALSE(isalnum(character) || character == '/' || character == '_' || character == '-',
                            ESP_ERR_INVALID_ARG,
                            TAG,
                            "Invalid base topic character");
    }

    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle),
                        TAG,
                        "Failed to open runtime config for writing");
    esp_err_t ret = nvs_set_str(nvs_handle, "base_topic", base_topic);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to save base topic");

    strlcpy(s_app_config.base_topic, base_topic, sizeof(s_app_config.base_topic));
    ESP_LOGI(TAG, "Base topic updated to '%s'", s_app_config.base_topic);
    return ESP_OK;
}

esp_err_t app_config_set_display_name(const char *display_name) {
    if (display_name == NULL) return ESP_ERR_INVALID_ARG;
    const size_t length = strnlen(display_name, APP_DISPLAY_NAME_MAX_LEN + 1);
    if (length == 0 || length > APP_DISPLAY_NAME_MAX_LEN) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char) display_name[i] < 0x20 || display_name[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Open display config");
    esp_err_t ret = nvs_set_str(handle, "display_name", display_name);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;
    strlcpy(s_app_config.display_name, display_name, sizeof(s_app_config.display_name));
    return ESP_OK;
}

esp_err_t app_config_display_hostname(const char *display_name, char *hostname, size_t hostname_size) {
    if (display_name == NULL || hostname == NULL || hostname_size < 2) return ESP_ERR_INVALID_ARG;
    size_t out = 0;
    bool dash = false;
    for (size_t i = 0; display_name[i] != '\0' && out + 1 < hostname_size; ++i) {
        const unsigned char c = (unsigned char) display_name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            hostname[out++] = (char) ((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
            dash = false;
        }
        else if (!dash && out > 0) { hostname[out++] = '-'; dash = true; }
    }
    while (out > 0 && hostname[out - 1] == '-') out--;
    hostname[out] = '\0';
    if (out == 0) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

const char *app_config_default_page_name(app_default_page_t page) {
    return panel_page_name((panel_page_id_t)page);
}

esp_err_t app_config_set_pages(const char *json) {
    panel_layout_t candidate;
    if (!panel_layout_parse(json, &candidate)) return ESP_ERR_INVALID_ARG;
    char canonical[PANEL_LAYOUT_JSON_SIZE], previous[PANEL_LAYOUT_JSON_SIZE];
    if (!panel_layout_json(&candidate, canonical, sizeof(canonical))) return ESP_ERR_NO_MEM;
    if (panel_layout_json(&s_app_config.layout, previous, sizeof(previous)) && strcmp(canonical, previous) == 0)
        return ESP_OK;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Open layout config");
    esp_err_t ret = nvs_set_str(handle, "pages", canonical);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;
    s_app_config.layout = candidate;
    s_app_config.default_page = (app_default_page_t)candidate.default_page;
    return ESP_OK;
}

esp_err_t app_config_set_default_page(const char *page_name) {
    panel_layout_t candidate = s_app_config.layout;
    if (!panel_page_parse(page_name, &candidate.default_page) ||
        !panel_layout_contains(&candidate, candidate.default_page)) return ESP_ERR_INVALID_ARG;
    char json[PANEL_LAYOUT_JSON_SIZE];
    if (!panel_layout_json(&candidate, json, sizeof(json))) return ESP_ERR_NO_MEM;
    return app_config_set_pages(json);
}
