#include "walldisplay/ha_discovery.h"

#include "walldisplay/app_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ha_discovery";

static void make_unique_id(char *dest, size_t dest_size, const char *suffix) {
    const app_config_t *config = app_config_get();
    size_t offset = (size_t) snprintf(dest, dest_size, "walldisplay_");

    for (const char *character = config->base_topic; *character != '\0' && offset + 1 < dest_size; ++character) {
        dest[offset++] = (*character >= 'a' && *character <= 'z') ||
                               (*character >= 'A' && *character <= 'Z') ||
                               (*character >= '0' && *character <= '9')
                           ? *character
                           : '_';
    }
    if (suffix != NULL && offset + 1 < dest_size) {
        dest[offset++] = '_';
        strlcpy(dest + offset, suffix, dest_size - offset);
    } else {
        dest[offset] = '\0';
    }
}

static esp_err_t publish_discovery(esp_mqtt_client_handle_t client,
                                   const char *component,
                                   const char *object_suffix,
                                   const char *name,
                                   const char *command_suffix,
                                   const char *state_suffix) {
    const app_config_t *config = app_config_get();
    char object_id[APP_TOPIC_MAX_LEN + 32];
    char topic[(APP_TOPIC_MAX_LEN * 2) + 64];
    char availability_topic[APP_TOPIC_MAX_LEN + 16];
    char command_topic[APP_TOPIC_MAX_LEN + 32];
    char state_topic[APP_TOPIC_MAX_LEN + 32];
    make_unique_id(object_id, sizeof(object_id), object_suffix);
    snprintf(topic, sizeof(topic), "%s/%s/%s/config", config->discovery_prefix, component, object_id);
    snprintf(availability_topic, sizeof(availability_topic), "%s/status", config->base_topic);

    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "uniq_id", object_id);
    if (command_suffix != NULL) {
        snprintf(command_topic, sizeof(command_topic), "%s/%s", config->base_topic, command_suffix);
        cJSON_AddStringToObject(root, "cmd_t", command_topic);
    }
    if (state_suffix != NULL) {
        snprintf(state_topic, sizeof(state_topic), "%s/%s", config->base_topic, state_suffix);
        cJSON_AddStringToObject(root, "stat_t", state_topic);
    }
    cJSON_AddStringToObject(root, "avty_t", availability_topic);
    cJSON_AddItemToObject(root, "dev", device);
    cJSON_AddStringToObject(device, "name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(device, "mdl", APP_DEVICE_MODEL);
    cJSON_AddStringToObject(device, "mf", APP_MANUFACTURER);
    cJSON_AddStringToObject(device, "sw", APP_FW_VERSION);
    char device_id[APP_TOPIC_MAX_LEN + 16];
    make_unique_id(device_id, sizeof(device_id), "device");
    cJSON_AddItemToObject(device, "ids", cJSON_CreateStringArray((const char *[]) { device_id }, 1));

    char *payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_publish(client, topic, payload, 0, 1, true);
    ESP_LOGI(TAG, "Published discovery: %s", topic);

    cJSON_free(payload);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t publish_device_diagnostic(esp_mqtt_client_handle_t client,
                                           const char *object_suffix,
                                           const char *name,
                                           const char *json_key,
                                           const char *unit,
                                           const char *device_class,
                                           const char *icon) {
    const app_config_t *config = app_config_get();
    char object_id[APP_TOPIC_MAX_LEN + 32];
    char topic[(APP_TOPIC_MAX_LEN * 2) + 64];
    char state_topic[APP_TOPIC_MAX_LEN + 32];
    char availability_topic[APP_TOPIC_MAX_LEN + 16];
    char value_template[64];
    make_unique_id(object_id, sizeof(object_id), object_suffix);
    snprintf(topic, sizeof(topic), "%s/sensor/%s/config", config->discovery_prefix, object_id);
    snprintf(state_topic, sizeof(state_topic), "%s/state/device", config->base_topic);
    snprintf(availability_topic, sizeof(availability_topic), "%s/status", config->base_topic);
    snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", json_key);

    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "uniq_id", object_id);
    cJSON_AddStringToObject(root, "stat_t", state_topic);
    cJSON_AddStringToObject(root, "val_tpl", value_template);
    cJSON_AddStringToObject(root, "avty_t", availability_topic);
    cJSON_AddStringToObject(root, "ent_cat", "diagnostic");
    if (unit != NULL) {
        cJSON_AddStringToObject(root, "unit_of_meas", unit);
    }
    if (device_class != NULL) {
        cJSON_AddStringToObject(root, "dev_cla", device_class);
    }
    if (icon != NULL) {
        cJSON_AddStringToObject(root, "icon", icon);
    }
    cJSON_AddItemToObject(root, "dev", device);
    cJSON_AddStringToObject(device, "name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(device, "mdl", APP_DEVICE_MODEL);
    cJSON_AddStringToObject(device, "mf", APP_MANUFACTURER);
    cJSON_AddStringToObject(device, "sw", APP_FW_VERSION);
    char device_id[APP_TOPIC_MAX_LEN + 16];
    make_unique_id(device_id, sizeof(device_id), "device");
    cJSON_AddItemToObject(device, "ids", cJSON_CreateStringArray((const char *[]) { device_id }, 1));

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const int message_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, true);
    cJSON_free(payload);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t ha_discovery_publish_all(esp_mqtt_client_handle_t client) {
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(publish_discovery(client,
                                      "sensor",
                                      "weather",
                                      "Weather Summary",
                                      NULL,
                                      "state/weather"));
    ESP_ERROR_CHECK(publish_discovery(client,
                                      "sensor",
                                      "media",
                                      "Now Playing",
                                      NULL,
                                      "state/media"));
    ESP_ERROR_CHECK(publish_discovery(client, "text", "name", "Panel Name", "set/name", "state/name"));
    ESP_ERROR_CHECK(publish_discovery(client, "sensor", "clock", "Panel Time", NULL, "state/clock"));
    ESP_ERROR_CHECK(publish_discovery(client, "sensor", "date", "Panel Date", NULL, "state/date"));
    for (int i = 1; i <= 4; ++i) {
        char object_id[32];
        char name[32];
        char state_suffix[32];
        snprintf(object_id, sizeof(object_id), "chip%d", i);
        snprintf(name, sizeof(name), "Panel Chip %d", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/chip%d", i);
        ESP_ERROR_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
        snprintf(object_id, sizeof(object_id), "chip%d_color", i);
        snprintf(name, sizeof(name), "Panel Chip %d Color", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/chip%d/color", i);
        ESP_ERROR_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
    }
    ESP_ERROR_CHECK(publish_discovery(client, "button", "sync", "Sync Panel", "cmd/sync", NULL));
    ESP_ERROR_CHECK(publish_discovery(client, "button", "wake", "Wake Panel", "cmd/wake", NULL));
    ESP_ERROR_CHECK(publish_discovery(client, "button", "screenshot", "Capture Screenshot", "cmd/screenshot", NULL));
    ESP_ERROR_CHECK(publish_discovery(client, "text", "base_topic", "Panel MQTT Topic", "cmd/config/base_topic", "state/config/base_topic"));
    ESP_ERROR_CHECK(publish_discovery(client, "text", "update_manifest", "Panel Update Manifest URL", "cmd/update", NULL));

    for (int i = 1; i <= 5; ++i) {
        char object_id[32];
        char name[32];
        char command_suffix[32];
        char state_suffix[32];
        snprintf(object_id, sizeof(object_id), "button%d", i);
        snprintf(name, sizeof(name), "Panel Button %d", i);
        snprintf(command_suffix, sizeof(command_suffix), "cmd/button%d", i);
        ESP_ERROR_CHECK(publish_discovery(client, "button", object_id, name, command_suffix, NULL));

        snprintf(object_id, sizeof(object_id), "button%d_state", i);
        snprintf(name, sizeof(name), "Panel Button %d State", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/button%d/state", i);
        ESP_ERROR_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
    }

    ESP_ERROR_CHECK(publish_device_diagnostic(client, "ip", "IP Address", "ip", NULL, NULL, "mdi:ip-network"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "mac", "Wi-Fi MAC", "mac", NULL, NULL, "mdi:network-outline"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "firmware", "Firmware Version", "firmware_version", NULL, NULL, "mdi:chip"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "blueprint", "Blueprint Version", "blueprint_version", NULL, NULL, "mdi:script-text"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "firmware_contract", "Firmware Contract", "firmware_contract_version", NULL, NULL, "mdi:file-document-check"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "blueprint_contract", "Blueprint Contract", "blueprint_contract_version", NULL, NULL, "mdi:file-document-check"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "contract_match", "Contract Compatible", "contract_match", NULL, NULL, "mdi:check-decagram"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "partition", "Running Partition", "app_partition", NULL, NULL, "mdi:memory"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "reset_reason", "Last Reset Reason", "reset_reason", NULL, NULL, "mdi:restart"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "ssid", "Wi-Fi SSID", "ssid", NULL, NULL, "mdi:wifi"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "bssid", "Wi-Fi BSSID", "wifi_bssid", NULL, NULL, "mdi:wifi-cog"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "channel", "Wi-Fi Channel", "wifi_channel", NULL, NULL, "mdi:wifi"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "rssi", "Wi-Fi Signal", "wifi_rssi", "dBm", "signal_strength", "mdi:wifi"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "uptime", "Uptime", "uptime_s", "s", "duration", "mdi:timer-outline"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "heap", "Free Heap", "free_heap", "B", "data_size", "mdi:memory"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "min_heap", "Minimum Free Heap", "min_free_heap", "B", "data_size", "mdi:memory"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "psram", "Free PSRAM", "free_psram", "B", "data_size", "mdi:memory"));
    ESP_ERROR_CHECK(publish_device_diagnostic(client, "min_psram", "Minimum Free PSRAM", "min_free_psram", "B", "data_size", "mdi:memory"));
    return ESP_OK;
}
