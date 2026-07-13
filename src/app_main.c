#include "walldisplay/app_config.h"
#include "walldisplay/display_board.h"
#include "walldisplay/device_info.h"
#include "walldisplay/ha_discovery.h"
#include "walldisplay/mqtt_app.h"
#include "walldisplay/ota_manager.h"
#include "walldisplay/ui.h"
#include "walldisplay/wifi_manager.h"

#include "cJSON.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

static esp_err_t publish_runtime_topic(const char *topic_suffix, const char *payload, bool retain) {
    const app_config_t *config = app_config_get();
    char topic[APP_TOPIC_MAX_LEN + 32];

    if (topic_suffix == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(topic, sizeof(topic), "%s/%s", config->base_topic, topic_suffix);
    return mqtt_app_publish(topic, payload, retain);
}

static void publish_discovery_if_enabled(esp_mqtt_client_handle_t client) {
    const app_config_t *config = app_config_get();

    if (!config->enable_discovery) {
        ESP_LOGI(TAG, "Home Assistant discovery disabled by config");
        return;
    }

    ESP_LOGI(TAG, "Publishing Home Assistant discovery");
    ESP_ERROR_CHECK(ha_discovery_publish_all(client));
}

static void subscribe_runtime_topics(esp_mqtt_client_handle_t client) {
    const app_config_t *config = app_config_get();
    char topic[APP_TOPIC_MAX_LEN + 32];

    snprintf(topic, sizeof(topic), "%s/set/weather", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/state/weather", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/set/media", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/state/media", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/set/name", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/state/name", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/set/clock", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/state/clock", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/set/date", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/state/date", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/set/blueprint_info", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/cmd/update", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    snprintf(topic, sizeof(topic), "%s/cmd/config/base_topic", config->base_topic);
    esp_mqtt_client_subscribe(client, topic, 1);

    for (int i = 1; i <= 5; ++i) {
        snprintf(topic, sizeof(topic), "%s/set/button%d/label", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/state/button%d/label", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/set/button%d/state", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/state/button%d/state", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);
    }

    for (int i = 1; i <= 4; ++i) {
        snprintf(topic, sizeof(topic), "%s/set/chip%d", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/state/chip%d", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/set/chip%d/color", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);

        snprintf(topic, sizeof(topic), "%s/state/chip%d/color", config->base_topic, i);
        esp_mqtt_client_subscribe(client, topic, 1);
    }

    esp_mqtt_client_subscribe(client, "homeassistant/status", 1);
}

static void on_mqtt_connected(esp_mqtt_client_handle_t client, void *user_ctx) {
    LV_UNUSED(user_ctx);
    const app_config_t *config = app_config_get();
    char availability_topic[APP_TOPIC_MAX_LEN + 16];

    publish_discovery_if_enabled(client);
    subscribe_runtime_topics(client);
    snprintf(availability_topic, sizeof(availability_topic), "%s/status", config->base_topic);
    ESP_ERROR_CHECK(mqtt_app_publish(availability_topic, "online", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ota_manager_mark_running_image_valid());
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/update", "{\"state\":\"idle\",\"version\":\"" APP_FW_VERSION "\"}", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/contract_version", APP_CONTRACT_VERSION, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/base_topic", config->base_topic, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("cmd/sync", "request", false));
    ESP_ERROR_CHECK_WITHOUT_ABORT(device_info_publish());
    ui_set_connection_status("MQTT connected");
    ui_set_mqtt_state("MQTT ok");
}

static void on_mqtt_message(const char *topic, const char *payload, void *user_ctx) {
    LV_UNUSED(user_ctx);

    const app_config_t *config = app_config_get();
    char expected_topic[APP_TOPIC_MAX_LEN + 32];

    ESP_LOGI(TAG, "Panel update received: %s", topic);

    snprintf(expected_topic, sizeof(expected_topic), "%s/cmd/update", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        esp_err_t err = ota_manager_request(payload);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Rejected OTA request: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/update", "{\"state\":\"error\",\"detail\":\"Invalid or busy update request\"}", true));
        }
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/cmd/config/base_topic", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        const esp_err_t err = app_config_set_base_topic(payload);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Rejected base-topic update: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/error", "Invalid Panel MQTT Topic", true));
            return;
        }

        ESP_LOGI(TAG, "Restarting to apply new MQTT topic");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/weather", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_weather_text(payload);
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/weather", payload, true));
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/state/weather", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_weather_text(payload);
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/media", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_media_text(payload);
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/media", payload, true));
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/state/media", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_media_text(payload);
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/name", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_title_text(payload);
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/name", payload, true));
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/state/name", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_title_text(payload);
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/clock", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_clock_text(payload);
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/clock", payload, true));
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/state/clock", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_clock_text(payload);
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/date", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_date_text(payload);
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/date", payload, true));
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/state/date", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        ui_set_date_text(payload);
        return;
    }

    snprintf(expected_topic, sizeof(expected_topic), "%s/set/blueprint_info", config->base_topic);
    if (strcmp(topic, expected_topic) == 0) {
        cJSON *blueprint_info = cJSON_Parse(payload);
        bool contract_matches = false;

        if (cJSON_IsObject(blueprint_info)) {
            const cJSON *version = cJSON_GetObjectItemCaseSensitive(blueprint_info, "version");
            const cJSON *contract = cJSON_GetObjectItemCaseSensitive(blueprint_info, "contract");
            if (cJSON_IsString(version) && cJSON_IsString(contract)) {
                contract_matches = device_info_set_blueprint_info(version->valuestring, contract->valuestring);
            } else {
                ESP_LOGW(TAG, "Ignoring malformed blueprint information payload");
                device_info_set_blueprint_info(NULL, NULL);
            }
        } else {
            ESP_LOGW(TAG, "Ignoring non-JSON blueprint information payload");
            device_info_set_blueprint_info(NULL, NULL);
        }
        cJSON_Delete(blueprint_info);
        ESP_ERROR_CHECK_WITHOUT_ABORT(device_info_publish());
        if (!contract_matches) {
            ESP_LOGE(TAG, "Blueprint information is incompatible with firmware contract '%s'", APP_CONTRACT_VERSION);
            ui_set_connection_status("Blueprint contract mismatch");
            ui_set_ha_state("HA !");
        }
        return;
    }

    for (int i = 1; i <= 5; ++i) {
        snprintf(expected_topic, sizeof(expected_topic), "%s/set/button%d/label", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_button_label((size_t) (i - 1), payload);
            char state_suffix[32];
            snprintf(state_suffix, sizeof(state_suffix), "state/button%d/label", i);
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic(state_suffix, payload, true));
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/state/button%d/label", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_button_label((size_t) (i - 1), payload);
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/set/button%d/state", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_button_state((size_t) (i - 1), payload);
            char state_suffix[32];
            snprintf(state_suffix, sizeof(state_suffix), "state/button%d/state", i);
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic(state_suffix, payload, true));
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/state/button%d/state", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_button_state((size_t) (i - 1), payload);
            return;
        }
    }

    for (int i = 1; i <= 4; ++i) {
        snprintf(expected_topic, sizeof(expected_topic), "%s/set/chip%d", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_measurement_chip((size_t) (i - 1), payload);
            char state_suffix[24];
            snprintf(state_suffix, sizeof(state_suffix), "state/chip%d", i);
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic(state_suffix, payload, true));
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/state/chip%d", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_measurement_chip((size_t) (i - 1), payload);
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/set/chip%d/color", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_measurement_chip_color((size_t) (i - 1), payload);
            char state_suffix[30];
            snprintf(state_suffix, sizeof(state_suffix), "state/chip%d/color", i);
            ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic(state_suffix, payload, true));
            return;
        }

        snprintf(expected_topic, sizeof(expected_topic), "%s/state/chip%d/color", config->base_topic, i);
        if (strcmp(topic, expected_topic) == 0) {
            ui_set_measurement_chip_color((size_t) (i - 1), payload);
            return;
        }
    }

    if (strcmp(topic, "homeassistant/status") == 0) {
        if (strcmp(payload, "online") == 0) {
            esp_mqtt_client_handle_t client = mqtt_app_client();
            if (client != NULL) {
                publish_discovery_if_enabled(client);
            }
            ui_set_connection_status("Home Assistant online");
            ui_set_ha_state("HA ok");
        } else {
            ui_set_connection_status("Home Assistant offline");
            ui_set_ha_state("HA off");
        }
    }
}

void app_main(void) {
    display_board_handle_t board = {0};
    esp_err_t err;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(ota_manager_init(publish_runtime_topic));
    ESP_ERROR_CHECK(device_info_init(publish_runtime_topic));

    ESP_LOGI(TAG, "Starting %s (%s)", APP_DEVICE_NAME, APP_FW_VERSION);

    ESP_ERROR_CHECK(display_board_init(&board));
    ESP_ERROR_CHECK(ui_init(&board));
    ui_set_connection_status("Connecting Wi-Fi...");
    ui_set_wifi_state("WiFi...");
    ui_set_mqtt_state("MQTT...");
    ui_set_ha_state("HA...");

    err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
        ui_set_connection_status("Wi-Fi not configured");
        ui_set_wifi_state("WiFi off");
        return;
    }

    ui_set_wifi_state("WiFi ok");

    err = mqtt_app_start(on_mqtt_connected, on_mqtt_message, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        ui_set_connection_status("MQTT start failed");
        ui_set_mqtt_state("MQTT off");
        return;
    }
}
