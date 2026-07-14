#pragma once

#include "esp_err.h"
#include "mqtt_client.h"

typedef void (*mqtt_connected_cb_t)(esp_mqtt_client_handle_t client, void *user_ctx);
typedef void (*mqtt_message_cb_t)(const char *topic, const char *payload, void *user_ctx);

esp_err_t mqtt_app_start(mqtt_connected_cb_t on_connected, mqtt_message_cb_t on_message, void *user_ctx);
esp_mqtt_client_handle_t mqtt_app_client(void);
esp_err_t mqtt_app_publish(const char *topic, const char *payload, bool retain);
/* Safe to call from LVGL event callbacks; publishing happens in a worker task. */
esp_err_t mqtt_app_publish_async(const char *topic, const char *payload, bool retain);
