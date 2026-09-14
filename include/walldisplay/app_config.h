#pragma once

#include "esp_err.h"
#include "page_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define APP_DEVICE_ID "guition-4848s040"
#define APP_DEVICE_NAME "Guition Wall Panel"
#define APP_DEVICE_MODEL "ESP32-4848S040"
#define APP_MANUFACTURER "Guition"
/* Generated from pyproject.toml by tools/sync_project_version.py.
 * Tagged release builds may override this at compile time with their exact
 * release tag, while the checked-in development version remains canonical. */
#ifndef APP_FW_VERSION
#define APP_FW_VERSION "1.0.0"
#endif
/* Increment whenever MQTT topics, payloads, or semantics change. */
#define APP_CONTRACT_VERSION "10"

#define APP_WIFI_MAX_SSID_LEN 32
#define APP_WIFI_MAX_PASSWORD_LEN 64
#define APP_MQTT_URI_MAX_LEN 128
#define APP_MQTT_USERNAME_MAX_LEN 64
#define APP_MQTT_PASSWORD_MAX_LEN 64
#define APP_TOPIC_MAX_LEN 128
#define APP_DISPLAY_NAME_MAX_LEN 64

typedef enum {
    APP_DEFAULT_PAGE_WEATHER,
    APP_DEFAULT_PAGE_MEDIA,
    APP_DEFAULT_PAGE_BUTTONS,
    APP_DEFAULT_PAGE_ABOUT,
} app_default_page_t;

typedef struct {
    char wifi_ssid[APP_WIFI_MAX_SSID_LEN + 1];
    char wifi_password[APP_WIFI_MAX_PASSWORD_LEN + 1];
    char mqtt_uri[APP_MQTT_URI_MAX_LEN + 1];
    char mqtt_username[APP_MQTT_USERNAME_MAX_LEN + 1];
    char mqtt_password[APP_MQTT_PASSWORD_MAX_LEN + 1];
    char discovery_prefix[APP_TOPIC_MAX_LEN + 1];
    char base_topic[APP_TOPIC_MAX_LEN + 1];
    char display_name[APP_DISPLAY_NAME_MAX_LEN + 1];
    bool enable_discovery;
    bool mqtt_require_tls;
    const char *mqtt_ca_certificate;
    const char *screenshot_token;
    app_default_page_t default_page;
    panel_layout_t layout;
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);
esp_err_t app_config_set_base_topic(const char *base_topic);
esp_err_t app_config_set_default_page(const char *page_name);
const char *app_config_default_page_name(app_default_page_t page);

esp_err_t app_config_set_pages(const char *json);
/** Store a human-readable label; the Wi-Fi hostname is derived separately. */
esp_err_t app_config_set_display_name(const char *display_name);
/** Return the deterministic single-label hostname for a display label. */
esp_err_t app_config_display_hostname(const char *display_name, char *hostname, size_t hostname_size);
