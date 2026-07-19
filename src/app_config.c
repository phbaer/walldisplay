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
    .enable_discovery = APPCFG_DEFAULT_ENABLE_DISCOVERY,
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
        uint8_t default_page;
        if (nvs_get_u8(nvs_handle, "default_page", &default_page) == ESP_OK && default_page <= APP_DEFAULT_PAGE_MEDIA) {
            s_app_config.default_page = (app_default_page_t)default_page;
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

const char *app_config_default_page_name(app_default_page_t page) {
    return page == APP_DEFAULT_PAGE_MEDIA ? "media" : "weather";
}

esp_err_t app_config_set_default_page(const char *page_name) {
    app_default_page_t page;
    nvs_handle_t nvs_handle;

    ESP_RETURN_ON_FALSE(page_name != NULL, ESP_ERR_INVALID_ARG, TAG, "Default page is null");
    if (strcasecmp(page_name, "weather") == 0) {
        page = APP_DEFAULT_PAGE_WEATHER;
    } else if (strcasecmp(page_name, "media") == 0) {
        page = APP_DEFAULT_PAGE_MEDIA;
    } else {
        ESP_LOGW(TAG, "Invalid default page '%s'", page_name);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle),
                        TAG,
                        "Failed to open runtime config for writing");
    esp_err_t ret = nvs_set_u8(nvs_handle, "default_page", (uint8_t)page);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to save default page");

    s_app_config.default_page = page;
    ESP_LOGI(TAG, "Default page updated to '%s'", app_config_default_page_name(page));
    return ESP_OK;
}
