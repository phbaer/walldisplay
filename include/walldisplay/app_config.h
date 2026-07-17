#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define APP_DEVICE_ID "guition-4848s040"
#define APP_DEVICE_NAME "Guition Wall Panel"
#define APP_DEVICE_MODEL "ESP32-4848S040"
#define APP_MANUFACTURER "Guition"
#define APP_FW_VERSION "0.5.0"
/* Increment whenever MQTT topics, payloads, or semantics change. */
#define APP_CONTRACT_VERSION "5"

#define APP_WIFI_MAX_SSID_LEN 32
#define APP_WIFI_MAX_PASSWORD_LEN 64
#define APP_MQTT_URI_MAX_LEN 128
#define APP_MQTT_USERNAME_MAX_LEN 64
#define APP_MQTT_PASSWORD_MAX_LEN 64
#define APP_TOPIC_MAX_LEN 128

typedef struct {
    char wifi_ssid[APP_WIFI_MAX_SSID_LEN + 1];
    char wifi_password[APP_WIFI_MAX_PASSWORD_LEN + 1];
    char mqtt_uri[APP_MQTT_URI_MAX_LEN + 1];
    char mqtt_username[APP_MQTT_USERNAME_MAX_LEN + 1];
    char mqtt_password[APP_MQTT_PASSWORD_MAX_LEN + 1];
    char discovery_prefix[APP_TOPIC_MAX_LEN + 1];
    char base_topic[APP_TOPIC_MAX_LEN + 1];
    bool enable_discovery;
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);
esp_err_t app_config_set_base_topic(const char *base_topic);
