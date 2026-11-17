#include "walldisplay/app_config.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "walldisplay/app_config_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdatomic.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *TAG = "app_config";
static const char *APP_CONFIG_PARTITION = "appcfg";
static const char *APP_CONFIG_NAMESPACE = "appcfg";
static const char *RUNTIME_CONFIG_NAMESPACE = "runtimecfg";
/* NVS keys are limited to 15 characters; keep the portal field name
 * independent from the compact on-flash key. */
#define RUNTIME_SCREENSHOT_TOKEN_KEY "screenshot_tok"
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
/* Readers on any task receive a pointer to an immutable snapshot. A writer
 * copies the current snapshot into the next slot, changes the copy, and then
 * publishes it, so a reader never observes a half-written string. A snapshot
 * stays untouched until CONFIG_SNAPSHOTS - 1 later updates have been published;
 * readers use the pointer only for the duration of one call, and updates are
 * rare, user-initiated events. Writers are serialized by s_write_lock, which
 * also keeps NVS and the published snapshot in the same order. */
#define CONFIG_SNAPSHOTS 3
typedef struct {
    app_config_t config;
    char screenshot_token[APP_SCREENSHOT_TOKEN_MAX_LEN + 1];
} config_snapshot_t;
static config_snapshot_t s_snapshots[CONFIG_SNAPSHOTS];
static size_t s_snapshot_index;
static _Atomic(const app_config_t *) s_current;
static SemaphoreHandle_t s_write_lock;
/* The CA certificate is too large to copy per snapshot. It changes only in a
 * portal save or a factory reset, both of which reboot the panel. */
static char s_runtime_mqtt_ca[APP_MQTT_CA_MAX_LEN + 1];
static bool s_initialized;

static void snapshot_set_token(config_snapshot_t *snapshot, const char *token) {
    strlcpy(snapshot->screenshot_token, token, sizeof(snapshot->screenshot_token));
    snapshot->config.screenshot_token = snapshot->screenshot_token;
}

/* Start an update from the published configuration. Call with the lock held. */
static config_snapshot_t *config_begin_update(void) {
    const config_snapshot_t *current = &s_snapshots[s_snapshot_index];
    config_snapshot_t *next = &s_snapshots[(s_snapshot_index + 1) % CONFIG_SNAPSHOTS];
    *next = *current;
    if (current->config.screenshot_token == current->screenshot_token)
        next->config.screenshot_token = next->screenshot_token;
    return next;
}

static void config_publish(config_snapshot_t *next) {
    s_snapshot_index = (size_t)(next - s_snapshots);
    atomic_store(&s_current, &next->config);
}

static void config_reset_to_defaults(app_config_t *config) {
    *config = s_default_config;
    panel_layout_defaults(&config->layout);
    (void) panel_layout_parse(APPCFG_DEFAULT_LAYOUT_JSON, &config->layout);
    config->default_page = (app_default_page_t)config->layout.default_page;
}

static void config_lock(void) { xSemaphoreTake(s_write_lock, portMAX_DELAY); }
static void config_unlock(void) { xSemaphoreGive(s_write_lock); }
esp_err_t app_config_display_hostname(const char *display_name, char *hostname, size_t hostname_size);

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

    if (s_write_lock == NULL) {
        s_write_lock = xSemaphoreCreateMutex();
        if (s_write_lock == NULL) return ESP_ERR_NO_MEM;
    }
    /* Initialization runs before any other task reads the configuration. */
    config_snapshot_t *snapshot = &s_snapshots[0];
    app_config_t *config = &snapshot->config;
    *config = s_default_config;
    panel_layout_defaults(&config->layout);
    if (!panel_layout_parse(APPCFG_DEFAULT_LAYOUT_JSON, &config->layout)) return ESP_ERR_INVALID_ARG;
    config->default_page = (app_default_page_t)config->layout.default_page;

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

            load_string_or_default(nvs_handle, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid), s_default_config.wifi_ssid);
            load_string_or_default(nvs_handle, "wifi_pass", config->wifi_password, sizeof(config->wifi_password), s_default_config.wifi_password);
            load_string_or_default(nvs_handle, "mqtt_uri", config->mqtt_uri, sizeof(config->mqtt_uri), s_default_config.mqtt_uri);
            load_string_or_default(nvs_handle, "mqtt_user", config->mqtt_username, sizeof(config->mqtt_username), s_default_config.mqtt_username);
            load_string_or_default(nvs_handle, "mqtt_pass", config->mqtt_password, sizeof(config->mqtt_password), s_default_config.mqtt_password);
            load_string_or_default(nvs_handle, "disc_pref", config->discovery_prefix, sizeof(config->discovery_prefix), s_default_config.discovery_prefix);
            load_string_or_default(nvs_handle, "base_topic", config->base_topic, sizeof(config->base_topic), s_default_config.base_topic);
            load_string_or_default(nvs_handle, "display_name", config->display_name, sizeof(config->display_name), s_default_config.display_name);

            uint8_t enable_discovery = s_default_config.enable_discovery;
            if (nvs_get_u8(nvs_handle, "discovery", &enable_discovery) == ESP_OK) {
                config->enable_discovery = enable_discovery != 0;
            }

            nvs_close(nvs_handle);
        }
    }

    ret = nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        uint8_t factory_reset = 0;
        if (nvs_get_u8(nvs_handle, "factory_reset", &factory_reset) == ESP_OK && factory_reset) {
            config_reset_to_defaults(config);
        }
        size_t required_size = sizeof(config->base_topic);
        nvs_get_str(nvs_handle, "base_topic", config->base_topic, &required_size);
        required_size = sizeof(config->display_name);
        nvs_get_str(nvs_handle, "display_name", config->display_name, &required_size);
        required_size = sizeof(config->wifi_ssid); nvs_get_str(nvs_handle, "wifi_ssid", config->wifi_ssid, &required_size);
        required_size = sizeof(config->wifi_password); nvs_get_str(nvs_handle, "wifi_pass", config->wifi_password, &required_size);
        required_size = sizeof(config->mqtt_uri); nvs_get_str(nvs_handle, "mqtt_uri", config->mqtt_uri, &required_size);
        required_size = sizeof(config->mqtt_username); nvs_get_str(nvs_handle, "mqtt_user", config->mqtt_username, &required_size);
        required_size = sizeof(config->mqtt_password); nvs_get_str(nvs_handle, "mqtt_pass", config->mqtt_password, &required_size);
        required_size = sizeof(config->discovery_prefix); nvs_get_str(nvs_handle, "disc_pref", config->discovery_prefix, &required_size);
        uint8_t mqtt_tls = config->mqtt_require_tls;
        if (nvs_get_u8(nvs_handle, "mqtt_tls", &mqtt_tls) == ESP_OK) config->mqtt_require_tls = mqtt_tls != 0;
        size_t ca_size = sizeof(s_runtime_mqtt_ca);
        if (nvs_get_str(nvs_handle, "mqtt_ca", s_runtime_mqtt_ca, &ca_size) == ESP_OK) {
            config->mqtt_ca_certificate = s_runtime_mqtt_ca;
        }
        size_t token_size = sizeof(snapshot->screenshot_token);
        if (nvs_get_str(nvs_handle, RUNTIME_SCREENSHOT_TOKEN_KEY, snapshot->screenshot_token, &token_size) == ESP_OK) {
            config->screenshot_token = snapshot->screenshot_token;
        }
        uint8_t default_page;
        if (nvs_get_u8(nvs_handle, "default_page", &default_page) == ESP_OK && default_page < PANEL_PAGE_COUNT &&
            panel_layout_contains(&config->layout, (panel_page_id_t)default_page)) {
            config->default_page = (app_default_page_t)default_page;
        }
        char layout_json[PANEL_LAYOUT_JSON_SIZE];
        size_t layout_size = sizeof(layout_json);
        config->layout.default_page = (panel_page_id_t)config->default_page;
        if (nvs_get_str(nvs_handle, "pages", layout_json, &layout_size) == ESP_OK &&
            panel_layout_parse(layout_json, &config->layout)) {
            config->default_page = (app_default_page_t)config->layout.default_page;
        }
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Runtime configuration loaded (Wi-Fi SSID length %u, MQTT %s)",
                 (unsigned)strnlen(config->wifi_ssid, sizeof(config->wifi_ssid)),
                 config->mqtt_uri[0] != '\0' ? "configured" : "not configured");
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to open runtime config");
    }

    s_snapshot_index = 0;
    atomic_store(&s_current, config);
    s_initialized = true;
    ESP_LOGI(TAG, "App config loaded");
    return ESP_OK;
}

const app_config_t *app_config_get(void) {
    const app_config_t *current = s_initialized ? atomic_load(&s_current) : NULL;
    return current != NULL ? current : &s_default_config;
}

static esp_err_t set_base_topic_locked(const char *base_topic) {
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

    config_snapshot_t *next = config_begin_update();
    strlcpy(next->config.base_topic, base_topic, sizeof(next->config.base_topic));
    config_publish(next);
    ESP_LOGI(TAG, "Base topic updated to '%s'", base_topic);
    return ESP_OK;
}

static esp_err_t set_display_name_locked(const char *display_name) {
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
    config_snapshot_t *next = config_begin_update();
    strlcpy(next->config.display_name, display_name, sizeof(next->config.display_name));
    config_publish(next);
    return ESP_OK;
}

static esp_err_t set_portal_config_locked(const char *ssid, const char *wifi_password,
                                       const char *display_name, const char *mqtt_uri,
                                       const char *mqtt_username, const char *mqtt_password,
                                       bool require_tls, const char *discovery_prefix,
                                       const char *base_topic, const char *screenshot_token,
                                       const char *mqtt_ca_certificate) {
    if (!ssid || !wifi_password || !display_name || !mqtt_uri || !mqtt_username ||
        !mqtt_password || !discovery_prefix || !base_topic || !screenshot_token || !mqtt_ca_certificate) return ESP_ERR_INVALID_ARG;
    const size_t ssid_len = strnlen(ssid, APP_WIFI_MAX_SSID_LEN + 1);
    const size_t wifi_pass_len = strnlen(wifi_password, APP_WIFI_MAX_PASSWORD_LEN + 1);
    const size_t name_len = strnlen(display_name, APP_DISPLAY_NAME_MAX_LEN + 1);
    const size_t uri_len = strnlen(mqtt_uri, APP_MQTT_URI_MAX_LEN + 1);
    const size_t user_len = strnlen(mqtt_username, APP_MQTT_USERNAME_MAX_LEN + 1);
    const size_t mqtt_pass_len = strnlen(mqtt_password, APP_MQTT_PASSWORD_MAX_LEN + 1);
    const size_t prefix_len = strnlen(discovery_prefix, APP_TOPIC_MAX_LEN + 1);
    const size_t topic_len = strnlen(base_topic, APP_TOPIC_MAX_LEN + 1);
    const size_t token_len = strnlen(screenshot_token, APP_SCREENSHOT_TOKEN_MAX_LEN + 1);
    const size_t ca_len = strnlen(mqtt_ca_certificate, APP_MQTT_CA_MAX_LEN + 1);
    if (ssid_len == 0 || ssid_len > APP_WIFI_MAX_SSID_LEN ||
        wifi_pass_len > APP_WIFI_MAX_PASSWORD_LEN || name_len == 0 ||
        name_len > APP_DISPLAY_NAME_MAX_LEN || uri_len > APP_MQTT_URI_MAX_LEN ||
        user_len > APP_MQTT_USERNAME_MAX_LEN || mqtt_pass_len > APP_MQTT_PASSWORD_MAX_LEN ||
        prefix_len == 0 || prefix_len > APP_TOPIC_MAX_LEN || topic_len == 0 ||
        topic_len > APP_TOPIC_MAX_LEN || token_len > APP_SCREENSHOT_TOKEN_MAX_LEN || ca_len > APP_MQTT_CA_MAX_LEN) return ESP_ERR_INVALID_ARG;
    if (uri_len && strncmp(mqtt_uri, "mqtt://", 7) != 0 && strncmp(mqtt_uri, "mqtts://", 8) != 0)
        return ESP_ERR_INVALID_ARG;
    if (app_config_display_hostname(display_name, (char[33]){0}, 33) != ESP_OK)
        return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < ssid_len; ++i) if ((unsigned char)ssid[i] < 0x20 || ssid[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < wifi_pass_len; ++i) if ((unsigned char)wifi_password[i] < 0x20 || wifi_password[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < name_len; ++i) if ((unsigned char)display_name[i] < 0x20 || display_name[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < uri_len; ++i) if ((unsigned char)mqtt_uri[i] < 0x20 || mqtt_uri[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < user_len; ++i) if ((unsigned char)mqtt_username[i] < 0x20 || mqtt_username[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < mqtt_pass_len; ++i) if ((unsigned char)mqtt_password[i] < 0x20 || mqtt_password[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < prefix_len; ++i) if ((unsigned char)discovery_prefix[i] < 0x21 || discovery_prefix[i] == '#') return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < topic_len; ++i) if ((unsigned char)base_topic[i] < 0x21 || base_topic[i] == '#') return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < token_len; ++i) if (!isalnum((unsigned char)screenshot_token[i])) return ESP_ERR_INVALID_ARG;
    if (token_len != 0 && token_len < 32) return ESP_ERR_INVALID_ARG;
    if (ca_len != 0 && (strstr(mqtt_ca_certificate, "-----BEGIN CERTIFICATE-----") == NULL ||
                        strstr(mqtt_ca_certificate, "-----END CERTIFICATE-----") == NULL)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < ca_len; ++i) {
        unsigned char c = (unsigned char)mqtt_ca_certificate[i];
        if ((c < 0x20 && c != '\n' && c != '\r') || c == 0x7f) return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Open portal config");
    esp_err_t ret = nvs_set_str(handle, "wifi_ssid", ssid);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "wifi_pass", wifi_password);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "display_name", display_name);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "mqtt_uri", mqtt_uri);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "mqtt_user", mqtt_username);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "mqtt_pass", mqtt_password);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "disc_pref", discovery_prefix);
    if (ret == ESP_OK) ret = nvs_set_str(handle, "base_topic", base_topic);
    if (ret == ESP_OK) ret = nvs_set_u8(handle, "mqtt_tls", require_tls ? 1 : 0);
    if (ret == ESP_OK) ret = nvs_set_str(handle, RUNTIME_SCREENSHOT_TOKEN_KEY, screenshot_token);
    if (ret == ESP_OK) {
        if (ca_len != 0) ret = nvs_set_str(handle, "mqtt_ca", mqtt_ca_certificate);
        else {
            ret = nvs_erase_key(handle, "mqtt_ca");
            if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
        }
    }
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;
    config_snapshot_t *next = config_begin_update();
    app_config_t *config = &next->config;
    strlcpy(config->wifi_ssid, ssid, sizeof(config->wifi_ssid));
    strlcpy(config->wifi_password, wifi_password, sizeof(config->wifi_password));
    strlcpy(config->display_name, display_name, sizeof(config->display_name));
    strlcpy(config->mqtt_uri, mqtt_uri, sizeof(config->mqtt_uri));
    strlcpy(config->mqtt_username, mqtt_username, sizeof(config->mqtt_username));
    strlcpy(config->mqtt_password, mqtt_password, sizeof(config->mqtt_password));
    strlcpy(config->discovery_prefix, discovery_prefix, sizeof(config->discovery_prefix));
    strlcpy(config->base_topic, base_topic, sizeof(config->base_topic));
    config->mqtt_require_tls = require_tls;
    if (ca_len != 0) {
        memcpy(s_runtime_mqtt_ca, mqtt_ca_certificate, ca_len + 1);
        config->mqtt_ca_certificate = s_runtime_mqtt_ca;
    } else {
        s_runtime_mqtt_ca[0] = '\0';
        config->mqtt_ca_certificate = APPCFG_MQTT_CA_CERTIFICATE;
    }
    snapshot_set_token(next, screenshot_token);
    config_publish(next);
    ESP_LOGI(TAG, "Portal configuration committed (Wi-Fi SSID length %u, MQTT %s)",
             (unsigned)ssid_len, mqtt_uri[0] != '\0' ? "configured" : "not configured");
    return ESP_OK;
}

static esp_err_t set_screenshot_token_locked(const char *token) {
    if (!token) return ESP_ERR_INVALID_ARG;
    size_t length = strnlen(token, APP_SCREENSHOT_TOKEN_MAX_LEN + 1);
    if (length > APP_SCREENSHOT_TOKEN_MAX_LEN || (length != 0 && length < 32)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i) if (!isalnum((unsigned char)token[i])) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Open security config");
    esp_err_t ret = nvs_set_str(handle, RUNTIME_SCREENSHOT_TOKEN_KEY, token);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;
    config_snapshot_t *next = config_begin_update();
    snapshot_set_token(next, token);
    config_publish(next);
    return ESP_OK;
}

static esp_err_t reset_runtime_locked(void) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    else if (ret == ESP_OK) {
        ret = nvs_erase_all(handle);
        if (ret == ESP_OK) ret = nvs_commit(handle);
        nvs_close(handle);
    }
    if (ret != ESP_OK) return ret;
    /* A signed factory image may contain credentials in the dedicated appcfg
     * partition. Reset must remove those as well as runtime overrides. */
    nvs_handle_t factory_handle;
    ret = nvs_open_from_partition(APP_CONFIG_PARTITION, APP_CONFIG_NAMESPACE, NVS_READWRITE, &factory_handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    else if (ret == ESP_OK) {
        ret = nvs_erase_all(factory_handle);
        if (ret == ESP_OK) ret = nvs_commit(factory_handle);
        nvs_close(factory_handle);
    }
    if (ret != ESP_OK) {
        esp_err_t deinit_ret = nvs_flash_deinit_partition(APP_CONFIG_PARTITION);
        if (deinit_ret == ESP_OK || deinit_ret == ESP_ERR_NVS_NOT_INITIALIZED) {
            ret = nvs_flash_erase_partition(APP_CONFIG_PARTITION);
            if (ret == ESP_ERR_NOT_FOUND) ret = ESP_OK;
        }
    }
    if (ret != ESP_OK) return ret;
    nvs_handle_t marker_handle;
    if (nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &marker_handle) == ESP_OK) {
        (void)nvs_set_u8(marker_handle, "factory_reset", 1);
        (void)nvs_commit(marker_handle);
        nvs_close(marker_handle);
    }
    config_snapshot_t *next = config_begin_update();
    config_reset_to_defaults(&next->config);
    next->screenshot_token[0] = '\0';
    config_publish(next);
    s_runtime_mqtt_ca[0] = '\0';
    ESP_LOGI(TAG, "Runtime configuration erased; setup AP will start after reboot");
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

esp_err_t app_config_derive_base_topic(const char *prefix, const char *display_name,
                                       char *base_topic, size_t base_topic_size) {
    if (!prefix || !display_name || !base_topic || base_topic_size < 3) return ESP_ERR_INVALID_ARG;
    const size_t prefix_len = strnlen(prefix, APP_TOPIC_MAX_LEN + 1);
    if (prefix_len == 0 || prefix_len > APP_TOPIC_MAX_LEN) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < prefix_len; ++i) {
        const unsigned char c = (unsigned char)prefix[i];
        if (c < 0x21 || c == '#' || c == '+') return ESP_ERR_INVALID_ARG;
    }
    char hostname[33];
    if (app_config_display_hostname(display_name, hostname, sizeof(hostname)) != ESP_OK) return ESP_ERR_INVALID_ARG;
    const int written = snprintf(base_topic, base_topic_size, "%s/%s", prefix, hostname);
    if (written < 0 || (size_t)written >= base_topic_size || written > APP_TOPIC_MAX_LEN) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

const char *app_config_default_page_name(app_default_page_t page) {
    return panel_page_name((panel_page_id_t)page);
}

static esp_err_t set_pages_locked(const char *json) {
    panel_layout_t candidate;
    if (!panel_layout_parse(json, &candidate)) return ESP_ERR_INVALID_ARG;
    char canonical[PANEL_LAYOUT_JSON_SIZE], previous[PANEL_LAYOUT_JSON_SIZE];
    if (!panel_layout_json(&candidate, canonical, sizeof(canonical))) return ESP_ERR_NO_MEM;
    if (panel_layout_json(&app_config_get()->layout, previous, sizeof(previous)) && strcmp(canonical, previous) == 0)
        return ESP_OK;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(RUNTIME_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Open layout config");
    esp_err_t ret = nvs_set_str(handle, "pages", canonical);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;
    config_snapshot_t *next = config_begin_update();
    next->config.layout = candidate;
    next->config.default_page = (app_default_page_t)candidate.default_page;
    config_publish(next);
    return ESP_OK;
}

static esp_err_t set_default_page_locked(const char *page_name) {
    panel_layout_t candidate = app_config_get()->layout;
    if (!panel_page_parse(page_name, &candidate.default_page) ||
        !panel_layout_contains(&candidate, candidate.default_page)) return ESP_ERR_INVALID_ARG;
    char json[PANEL_LAYOUT_JSON_SIZE];
    if (!panel_layout_json(&candidate, json, sizeof(json))) return ESP_ERR_NO_MEM;
    return set_pages_locked(json);
}

esp_err_t app_config_set_base_topic(const char *base_topic) {
    config_lock();
    const esp_err_t result = set_base_topic_locked(base_topic);
    config_unlock();
    return result;
}

esp_err_t app_config_set_display_name(const char *display_name) {
    config_lock();
    const esp_err_t result = set_display_name_locked(display_name);
    config_unlock();
    return result;
}

esp_err_t app_config_set_screenshot_token(const char *token) {
    config_lock();
    const esp_err_t result = set_screenshot_token_locked(token);
    config_unlock();
    return result;
}

esp_err_t app_config_reset_runtime(void) {
    config_lock();
    const esp_err_t result = reset_runtime_locked();
    config_unlock();
    return result;
}

esp_err_t app_config_set_pages(const char *json) {
    config_lock();
    const esp_err_t result = set_pages_locked(json);
    config_unlock();
    return result;
}

esp_err_t app_config_set_portal_config(const char *ssid, const char *wifi_password,
                                       const char *display_name, const char *mqtt_uri,
                                       const char *mqtt_username, const char *mqtt_password,
                                       bool require_tls, const char *discovery_prefix,
                                       const char *base_topic, const char *screenshot_token,
                                       const char *mqtt_ca_certificate) {    config_lock();
    const esp_err_t result = set_portal_config_locked(ssid, wifi_password, display_name, mqtt_uri, mqtt_username,
                                                      mqtt_password, require_tls, discovery_prefix, base_topic,
                                                      screenshot_token, mqtt_ca_certificate);
    config_unlock();
    return result;
}

esp_err_t app_config_set_default_page(const char *page_name) {
    config_lock();
    const esp_err_t result = set_default_page_locked(page_name);
    config_unlock();
    return result;
}
