#include "walldisplay/app_config.h"
#include "walldisplay/display_board.h"
#include "walldisplay/device_info.h"
#include "walldisplay/display_dimming.h"
#include "walldisplay/ha_discovery.h"
#include "walldisplay/mqtt_app.h"
#include "walldisplay/network_client.h"
#include "walldisplay/media_artwork.h"
#include "walldisplay/ota_manager.h"
#include "walldisplay/screenshot.h"
#include "walldisplay/ui.h"
#include "walldisplay/panel_commands.h"
#include "walldisplay/panel_topics.h"
#include "walldisplay/security_policy.h"
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
    char topic[APP_TOPIC_MAX_LEN + 72];

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
    const esp_err_t err = ha_discovery_publish_all(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Home Assistant discovery incomplete: %s; will retry on reconnect", esp_err_to_name(err));
    }
}

static void subscribe_panel_topic(const char *suffix, void *context) {
    char topic[APP_TOPIC_MAX_LEN + 48];
    snprintf(topic, sizeof(topic), "%s/%s", app_config_get()->base_topic, suffix);
    esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)context, topic, 1);
}

static void subscribe_runtime_topics(esp_mqtt_client_handle_t client) {
    panel_topics_for_each_subscription(subscribe_panel_topic, client);
    esp_mqtt_client_subscribe(client, "homeassistant/status", 1);
}

static void on_mqtt_connected(esp_mqtt_client_handle_t client, void *user_ctx) {
    LV_UNUSED(user_ctx);
    const app_config_t *config = app_config_get();
    char availability_topic[APP_TOPIC_MAX_LEN + 16];

    publish_discovery_if_enabled(client);
    subscribe_runtime_topics(client);
    snprintf(availability_topic, sizeof(availability_topic), "%s/status", config->base_topic);
    ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_app_publish(availability_topic, "online", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ota_manager_mark_running_image_valid());
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/update", "{\"state\":\"idle\",\"version\":\"" APP_FW_VERSION "\"}", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/contract_version", APP_CONTRACT_VERSION, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/base_topic", config->base_topic, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/name", config->display_name, true));
    char hostname[33];
    if (app_config_display_hostname(config->display_name, hostname, sizeof(hostname)) == ESP_OK)
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/hostname", hostname, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/page", ui_current_page_name(), true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/screenshots_enabled", screenshot_is_enabled() ? "ON" : "OFF", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/ap", wifi_manager_ap_active() ? "ON" : "OFF", true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/default_page", app_config_default_page_name(config->default_page), true));
    char layout_json[PANEL_LAYOUT_JSON_SIZE];
    if (panel_layout_json(&config->layout, layout_json, sizeof(layout_json)))
        ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("state/config/pages", layout_json, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic("cmd/sync", "request", false));
    /* Discovery can queue many retained messages. Publish diagnostics from
     * the periodic device-info timer after the MQTT outbox has drained. */
    ui_set_connection_status("MQTT connected");
    ui_set_mqtt_state("MQTT ok");
}

static void publish_state(const char *suffix, const char *payload) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(publish_runtime_topic(suffix, payload, true));
}

/* Echo an accepted "set/<name>" input as retained "state/<name>". Legacy
 * "state/<name>" inputs are already the retained state and are not echoed. */
static void echo_input(const char *suffix, bool canonical, const char *payload) {
    if (!canonical) return;
    char state[64];
    snprintf(state, sizeof(state), "state/%s", suffix + strlen("set/"));
    publish_state(state, payload);
}

/* Publish the stored layout. Reads the configuration again, because an update
 * publishes a new snapshot. */
static void publish_layout_state(void) {
    const app_config_t *config = app_config_get();
    char layout_json[PANEL_LAYOUT_JSON_SIZE];
    if (panel_layout_json(&config->layout, layout_json, sizeof(layout_json))) publish_state("state/config/pages", layout_json);
    publish_state("state/config/default_page", app_config_default_page_name(config->default_page));
}

static void request_artwork(const char *payload) {
    cJSON *media = cJSON_Parse(payload);
    const cJSON *url = cJSON_IsObject(media) ? cJSON_GetObjectItemCaseSensitive(media, "artwork_url") : NULL;
    ESP_LOGI(TAG, "Media artwork URL: %s", cJSON_IsString(url) && url->valuestring[0] ? "provided" : "missing");
    media_artwork_request(cJSON_IsString(url) ? url->valuestring : "");
    cJSON_Delete(media);
}

static void restart_after(const char *reason) {
    ESP_LOGW(TAG, "%s", reason);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

/* Non-retained "cmd/<command>" messages. */
static bool handle_command(const char *command, const char *payload) {
    if (strcmp(command, "update") == 0) {
        /* ota_manager_request publishes the specific reason for a refusal. */
        const esp_err_t err = ota_manager_request(payload);
        if (err != ESP_OK) ESP_LOGW(TAG, "Rejected OTA request: %s", esp_err_to_name(err));
        return true;
    }
    if (strcmp(command, "config/base_topic") == 0) {
        const esp_err_t err = app_config_set_base_topic(payload);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Rejected base-topic update: %s", esp_err_to_name(err));
            /* The rejected topic was not stored, so this reports on the current one. */
            publish_state("state/config/error", "Invalid Panel MQTT Topic");
            return true;
        }
        restart_after("Restarting to apply new MQTT topic");
        return true;
    }
    if (strcmp(command, "config/default_page") == 0) {
        if (app_config_set_default_page(payload) != ESP_OK || ui_apply_page_layout() != ESP_OK ||
            ui_show_page(payload) != ESP_OK) {
            ESP_LOGW(TAG, "Rejected default-page update");
            publish_state("state/config/error", "Invalid Default Widget");
            return true;
        }
        publish_layout_state();
        publish_state("state/config/error", "");
        return true;
    }
    if (strcmp(command, "wake") == 0) {
        display_dimming_wake();
        return true;
    }
    if (strcmp(command, "config/ap") == 0) {
        const esp_err_t err = strcmp(payload, "ON") == 0 ? wifi_manager_start_ap() :
                              strcmp(payload, "OFF") == 0 ? wifi_manager_stop_ap() : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK && wifi_manager_ap_active()) (void)ui_show_page("about");
        publish_state("state/config/ap", err == ESP_OK && wifi_manager_ap_active() ? "ON" : "OFF");
        return true;
    }
    if (strcmp(command, "config/factory_reset") == 0) {
        const esp_err_t err = app_config_reset_runtime();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Factory reset failed: %s", esp_err_to_name(err));
            publish_state("state/config/error", "Factory reset failed");
            return true;
        }
        restart_after("Factory reset requested over MQTT; rebooting into setup AP");
        return true;
    }
    /* "cmd/reboot" is the legacy name of "cmd/config/reboot". */
    if (strcmp(command, "reboot") == 0 || strcmp(command, "config/reboot") == 0) {
        restart_after("Reboot requested over MQTT");
        return true;
    }
    if (strcmp(command, "config/screenshot_token") == 0) {
        const esp_err_t err = app_config_set_screenshot_token(payload);
        publish_state("state/config/error",
                      err == ESP_OK ? "" : "Screenshot token must contain 32-64 ASCII letters and digits");
        return true;
    }
    if (strcmp(command, "screenshot") == 0) {
        const esp_err_t err = payload[0] == '\0' ? screenshot_request() : screenshot_request_named(payload);
        if (err != ESP_OK) ESP_LOGW(TAG, "Screenshot request rejected: %s", esp_err_to_name(err));
        return true;
    }
    if (strcmp(command, "page") == 0) {
        const esp_err_t err = ui_show_page(payload);
        if (err != ESP_OK) ESP_LOGW(TAG, "Page request rejected: %s", esp_err_to_name(err));
        return true;
    }
    /* The panel's own button, media, and sync commands arrive here as well. */
    return false;
}

static void handle_display_power(const char *suffix, bool canonical, const char *payload) {
    cJSON *display_power = cJSON_Parse(payload);
    const cJSON *dim_after = cJSON_IsObject(display_power) ? cJSON_GetObjectItemCaseSensitive(display_power, "dim_after") : NULL;
    const cJSON *off_after = cJSON_IsObject(display_power) ? cJSON_GetObjectItemCaseSensitive(display_power, "off_after") : NULL;
    const cJSON *dim_percent = cJSON_IsObject(display_power) ? cJSON_GetObjectItemCaseSensitive(display_power, "dim_percent") : NULL;
    const bool accepted =
        cJSON_IsNumber(dim_after) && cJSON_IsNumber(off_after) && cJSON_IsNumber(dim_percent) &&
        dim_after->valuedouble >= 0 && dim_after->valuedouble <= 86400 && off_after->valuedouble >= 0 &&
        off_after->valuedouble <= 86400 && dim_percent->valuedouble >= 0 && dim_percent->valuedouble <= 100 &&
        display_dimming_set_config((uint32_t) dim_after->valuedouble, (uint32_t) off_after->valuedouble,
                                   (uint8_t) dim_percent->valuedouble) == ESP_OK;
    cJSON_Delete(display_power);
    if (!accepted) {
        ESP_LOGW(TAG, "Ignoring invalid display-power configuration");
        return;
    }
    echo_input(suffix, canonical, payload);
}

static void handle_blueprint_info(const char *payload) {
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
}

/* Retained "set/<name>" configuration without a legacy "state/" alias. */
static bool handle_configuration(const char *suffix, const char *payload) {
    size_t index = 0;
    if (strcmp(suffix, "set/screenshots_enabled") == 0) {
        esp_err_t err = ESP_ERR_INVALID_ARG;
        if (!strcmp(payload, "ON") || !strcmp(payload, "OFF")) err = screenshot_set_enabled(!strcmp(payload, "ON"));
        publish_state("state/screenshots_enabled", err == ESP_OK ? payload : "OFF");
        return true;
    }
    if (strcmp(suffix, "set/pages") == 0) {
        esp_err_t err = app_config_set_pages(payload);
        if (err == ESP_OK) err = ui_apply_page_layout();
        publish_state("state/config/error", err == ESP_OK ? "" : "Invalid page layout or layout update failed");
        if (err == ESP_OK) {
            if (app_config_get()->enable_discovery)
                ESP_ERROR_CHECK_WITHOUT_ABORT(ha_discovery_publish_page_options(mqtt_app_client()));
            publish_layout_state();
        }
        return true;
    }
    if (panel_topic_index(suffix, "set/grid/", "", PANEL_GRID_BUTTON_COUNT, &index)) {
        const esp_err_t err = ui_set_grid_button(index, payload);
        publish_state("state/config/error", err == ESP_OK ? "" : "Invalid grid payload");
        if (err == ESP_OK) echo_input(suffix, true, payload);
        return true;
    }
    if (strcmp(suffix, "set/blueprint_info") == 0) {
        handle_blueprint_info(payload);
        return true;
    }
    return false;
}

/* Display data and settings, as "set/<name>" or the legacy "state/<name>". */
static bool handle_input(const char *suffix, const char *payload) {
    bool canonical = false;
    size_t index = 0;
    if (panel_topic_input(suffix, "display_power", &canonical)) {
        handle_display_power(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input(suffix, "weather", &canonical)) {
        ui_set_weather_text(payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input(suffix, "media/browse", &canonical)) {
        if (ui_set_media_browse(payload) != ESP_OK) ESP_LOGW(TAG, "Ignoring invalid media browse response");
        return true;
    }
    if (panel_topic_input(suffix, "media", &canonical)) {
        ui_set_media_text(payload);
        request_artwork(payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input(suffix, "media/power", &canonical)) {
        if (ui_set_media_power(payload) != ESP_OK) {
            ESP_LOGW(TAG, "Ignoring invalid media power configuration");
            return true;
        }
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "media/favorite", "/label", PANEL_MEDIA_FAVORITE_COUNT, &index, &canonical)) {
        ui_set_media_favorite_label(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "media/favorite", "/icon", PANEL_MEDIA_FAVORITE_COUNT, &index, &canonical)) {
        ui_set_media_favorite_icon(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input(suffix, "name", &canonical)) {
        if (wifi_manager_set_display_name(payload) != ESP_OK) {
            ESP_LOGW(TAG, "Ignoring invalid %s", canonical ? "display name" : "retained panel name");
            return true;
        }
        ui_set_title_text(payload);
        if (canonical) {
            publish_state("state/name", payload);
            char hostname[33];
            if (app_config_display_hostname(payload, hostname, sizeof(hostname)) == ESP_OK)
                publish_state("state/hostname", hostname);
        }
        return true;
    }
    if (panel_topic_input(suffix, "clock", &canonical)) {
        ui_set_clock_text(payload);
        // Both Home Assistant synchronization paths publish the retained clock
        // value every minute. Treat that existing sync traffic as a panel-local
        // heartbeat so a panel that connects after HA's global birth message
        // still gets a green Home Assistant indicator without a new MQTT topic.
        ui_set_ha_state("HA ok");
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input(suffix, "date", &canonical)) {
        ui_set_date_text(payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "button", "/label", PANEL_FOOTER_BUTTON_COUNT, &index, &canonical)) {
        ui_set_button_label(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "button", "/state", PANEL_FOOTER_BUTTON_COUNT, &index, &canonical)) {
        ui_set_button_state(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "chip", "", PANEL_CHIP_COUNT, &index, &canonical)) {
        ui_set_measurement_chip(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    if (panel_topic_input_index(suffix, "chip", "/color", PANEL_CHIP_COUNT, &index, &canonical)) {
        ui_set_measurement_chip_color(index, payload);
        echo_input(suffix, canonical, payload);
        return true;
    }
    return false;
}

static void handle_home_assistant_status(const char *payload) {
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

static void on_mqtt_message(const char *topic, const char *payload, bool retained, void *user_ctx) {
    LV_UNUSED(user_ctx);

    const char *base_topic = app_config_get()->base_topic;
    if (mqtt_replayed_command(base_topic, topic, retained)) {
        ESP_LOGW(TAG, "Ignoring retained command");
        return;
    }
    if (strcmp(topic, "homeassistant/status") == 0) {
        handle_home_assistant_status(payload);
        return;
    }
    /* Resolve the topic once; handlers compare the suffix below the base. */
    const char *suffix = panel_topic_suffix(base_topic, topic);
    if (suffix == NULL) return;
    const bool handled = strncmp(suffix, "cmd/", 4) == 0 ? handle_command(suffix + 4, payload)
                                                         : handle_configuration(suffix, payload) || handle_input(suffix, payload);
    if (handled) ESP_LOGI(TAG, "Panel update received: %s", suffix);
    else ESP_LOGD(TAG, "Ignoring unhandled topic: %s", suffix);
}

static void network_start_task(void *arg) {
    (void)arg;
    /* Router advertisements and Wi-Fi may arrive after the bounded boot wait.
     * Keep startup alive without blocking the display or the setup portal. */
    while (!wifi_manager_station_ready()) vTaskDelay(pdMS_TO_TICKS(1000));
    ui_set_wifi_state("WiFi ok");
    const esp_err_t err = mqtt_app_start(on_mqtt_connected, on_mqtt_message, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        ui_set_connection_status("MQTT start failed");
        ui_set_mqtt_state("MQTT off");
    }
    vTaskDelete(NULL);
}

void app_main(void) {
    display_board_handle_t board = {0};
    esp_err_t err;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(network_client_init());
    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(media_artwork_init());
    ESP_ERROR_CHECK(ota_manager_init(publish_runtime_topic));
    ESP_ERROR_CHECK(device_info_init(publish_runtime_topic));

    ESP_LOGI(TAG, "Starting %s (%s, build %s)", APP_DEVICE_NAME, APP_FW_VERSION, APP_BUILD_ID);

    ESP_ERROR_CHECK(display_board_init(&board));
    err = screenshot_init(&board, publish_runtime_topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Screenshot support disabled: %s", esp_err_to_name(err));
    }
    ui_actions_set_handler(panel_commands_handle, NULL);
    ESP_ERROR_CHECK(ui_init(&board));
    ESP_ERROR_CHECK(display_dimming_init());
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

    if (app_config_get()->display_name[0] != '\0') {
        (void) wifi_manager_set_display_name(app_config_get()->display_name);
        ui_set_title_text(app_config_get()->display_name);
    }

    if (!wifi_manager_station_ready()) {
        const bool ap_active = wifi_manager_ap_active();
        ui_set_connection_status(ap_active ? "Wi-Fi setup AP active" : "Waiting for network address...");
        ui_set_wifi_state(ap_active ? "Setup AP" : "WiFi...");
        if (ap_active) {
            (void)ui_show_page("about");
            return;
        }
    }
    if (xTaskCreate(network_start_task, "network_start", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not start network worker");
        ui_set_connection_status("Network startup failed");
    }
}
