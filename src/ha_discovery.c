#include "walldisplay/ha_discovery.h"

#include "walldisplay/app_config.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ha_discovery";

/* Discovery is replayed when MQTT reconnects.  A connection can disappear
 * while the retained discovery set is being published; never turn that
 * transient transport failure into an application abort. */
#define DISCOVERY_CHECK(expression) do { \
        const esp_err_t discovery_error = (expression); \
        if (discovery_error != ESP_OK) { \
            ESP_LOGW(TAG, "Discovery publish interrupted: %s", esp_err_to_name(discovery_error)); \
            return discovery_error; \
        } \
    } while (0)

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

static void make_device_name(char *dest, size_t dest_size, const app_config_t *config) {
    if (config->display_name[0] != '\0') {
        snprintf(dest, dest_size, "%s (%s)", APP_DEVICE_NAME, config->display_name);
    } else {
        strlcpy(dest, APP_DEVICE_NAME, dest_size);
    }
}

static esp_err_t publish_discovery_options(esp_mqtt_client_handle_t client,
                                   const char *component,
                                   const char *object_suffix,
                                   const char *name,
                                   const char *command_suffix,
                                   const char *state_suffix, const char *options_json) {
    const app_config_t *config = app_config_get();
    char object_id[APP_TOPIC_MAX_LEN + 32];
    char topic[(APP_TOPIC_MAX_LEN * 2) + 64];
    char availability_topic[APP_TOPIC_MAX_LEN + 16];
    char command_topic[APP_TOPIC_MAX_LEN + 32];
    char state_topic[APP_TOPIC_MAX_LEN + 32];
    char device_name[sizeof(APP_DEVICE_NAME) + APP_DISPLAY_NAME_MAX_LEN + 4];
    make_unique_id(object_id, sizeof(object_id), object_suffix);
    make_device_name(device_name, sizeof(device_name), config);
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
    if (strcmp(object_suffix, "screenshot") == 0) {
        cJSON_AddStringToObject(root, "pl_prs", "screenshot");
        cJSON_AddBoolToObject(root, "ret", false);
    }
    if (strcmp(object_suffix, "screenshot_token") == 0) {
        cJSON_AddStringToObject(root, "mode", "password");
        cJSON_AddStringToObject(root, "ent_cat", "config");
        cJSON_AddNumberToObject(root, "min", 32);
        cJSON_AddNumberToObject(root, "max", 64);
        cJSON_AddStringToObject(root, "ptrn", "[A-Za-z0-9]{32,64}");
        cJSON_AddBoolToObject(root, "ret", false);
    }
    if (strcmp(object_suffix, "weather") == 0 || strcmp(object_suffix, "media") == 0) {
        cJSON_AddStringToObject(root, "val_tpl", strcmp(object_suffix, "weather") == 0 ?
            "{{ value_json.condition | default('unknown') if value_json is defined else value[:255] }}" :
            "{{ value_json.state | default('unknown') if value_json is defined else value[:255] }}");
        cJSON_AddStringToObject(root, "json_attr_t", state_topic);
        cJSON_AddStringToObject(root, "json_attr_tpl", "{{ value_json | tojson if value_json is defined else '{}' }}");
    }
    if (strcmp(object_suffix, "screenshot_status") == 0) {
        cJSON_AddStringToObject(root, "val_tpl", "{{ value_json.state }}");
        cJSON_AddStringToObject(root, "json_attr_t", state_topic);
        cJSON_AddStringToObject(root, "ent_cat", "diagnostic");
    }
    cJSON_AddItemToObject(root, "dev", device);
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "mdl", APP_DEVICE_MODEL);
    cJSON_AddStringToObject(device, "mf", APP_MANUFACTURER);
    cJSON_AddStringToObject(device, "sw", APP_FW_VERSION);
    char device_id[APP_TOPIC_MAX_LEN + 16];
    make_unique_id(device_id, sizeof(device_id), "device");
    cJSON_AddItemToObject(device, "ids", cJSON_CreateStringArray((const char *[]) { device_id }, 1));

    if (options_json != NULL) {
        cJSON *options = cJSON_Parse(options_json);
        if (!cJSON_IsObject(options)) { cJSON_Delete(options); cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
        cJSON *item;
        cJSON_ArrayForEach(item, options) {
            cJSON_DeleteItemFromObjectCaseSensitive(root, item->string);
            if (strcmp(item->string, "json_attr_t") == 0 && cJSON_IsString(item) &&
                strcmp(item->valuestring, "$state") == 0) {
                cJSON_AddStringToObject(root, item->string, state_topic);
            } else cJSON_AddItemToObject(root, item->string, cJSON_Duplicate(item, true));
        }
        cJSON_Delete(options);
    }
    char *payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    const int message_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, true);
    ESP_LOGI(TAG, "Published discovery: %s", topic);

    cJSON_free(payload);
    cJSON_Delete(root);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t publish_discovery(esp_mqtt_client_handle_t client, const char *component,
                                   const char *id, const char *name, const char *command,
                                   const char *state) {
    return publish_discovery_options(client, component, id, name, command, state, NULL);
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
    char device_name[sizeof(APP_DEVICE_NAME) + APP_DISPLAY_NAME_MAX_LEN + 4];
    make_unique_id(object_id, sizeof(object_id), object_suffix);
    make_device_name(device_name, sizeof(device_name), config);
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
    cJSON_AddStringToObject(device, "name", device_name);
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
    const int message_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, true);
    cJSON_free(payload);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t publish_page_select(esp_mqtt_client_handle_t client, bool current) {
    const app_config_t *config = app_config_get();
    char object_id[APP_TOPIC_MAX_LEN + 32];
    char topic[(APP_TOPIC_MAX_LEN * 2) + 64];
    char command_topic[APP_TOPIC_MAX_LEN + 32];
    char state_topic[APP_TOPIC_MAX_LEN + 32];
    char availability_topic[APP_TOPIC_MAX_LEN + 16];
    char device_name[sizeof(APP_DEVICE_NAME) + APP_DISPLAY_NAME_MAX_LEN + 4];
    make_unique_id(object_id, sizeof(object_id), current ? "current_page" : "default_page");
    make_device_name(device_name, sizeof(device_name), config);
    snprintf(topic, sizeof(topic), "%s/select/%s/config", config->discovery_prefix, object_id);
    snprintf(command_topic, sizeof(command_topic), "%s/%s", config->base_topic, current ? "cmd/page" : "cmd/config/default_page");
    snprintf(state_topic, sizeof(state_topic), "%s/%s", config->base_topic, current ? "state/page" : "state/config/default_page");
    snprintf(availability_topic, sizeof(availability_topic), "%s/status", config->base_topic);

    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", current ? "Current Panel Page" : "Default Panel Widget");
    cJSON_AddBoolToObject(root, "ret", false);
    cJSON_AddStringToObject(root, "uniq_id", object_id);
    cJSON_AddStringToObject(root, "cmd_t", command_topic);
    cJSON_AddStringToObject(root, "stat_t", state_topic);
    cJSON_AddStringToObject(root, "avty_t", availability_topic);
    const char *options[PANEL_MAX_PAGES];
    for (size_t i = 0; i < config->layout.count; ++i) options[i] = panel_page_name(config->layout.order[i]);
    cJSON_AddItemToObject(root, "ops", cJSON_CreateStringArray(options, (int)config->layout.count));
    cJSON_AddItemToObject(root, "dev", device);
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "mdl", APP_DEVICE_MODEL);
    cJSON_AddStringToObject(device, "mf", APP_MANUFACTURER);
    cJSON_AddStringToObject(device, "sw", APP_FW_VERSION);
    char device_id[APP_TOPIC_MAX_LEN + 16];
    make_unique_id(device_id, sizeof(device_id), "device");
    cJSON_AddItemToObject(device, "ids", cJSON_CreateStringArray((const char *[]) { device_id }, 1));

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) return ESP_ERR_NO_MEM;
    const int message_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, true);
    cJSON_free(payload);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t ha_discovery_publish_page_options(esp_mqtt_client_handle_t client) {
    DISCOVERY_CHECK(publish_page_select(client, false));
    return publish_page_select(client, true);
}

typedef struct {
    const char *component, *id, *name, *command, *state, *options;
} discovery_endpoint_t;

static const discovery_endpoint_t s_endpoints[] = {
    {"switch", "screenshots_enabled", "Enable Screenshots", "set/screenshots_enabled", "state/screenshots_enabled", "{\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "update_status", "Firmware Update Status", NULL, "state/update", "{\"val_tpl\":\"{{ value_json.state | default('unknown') }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"sensor", "page_layout", "Page Layout", NULL, "state/config/pages", "{\"val_tpl\":\"{{ value_json.default_page | default('unknown') }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"sensor", "display_settings", "Display Settings", NULL, "state/display_power", "{\"val_tpl\":\"{{ value_json.dim_percent | default('unknown') }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"sensor", "media_power", "Media Power State", NULL, "state/media/power", "{\"val_tpl\":\"{{ value_json.state | default('unknown') }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"sensor", "device_diagnostics", "Device Diagnostics", NULL, "state/device", "{\"val_tpl\":\"{{ value_json.uptime_s | default('unknown') }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"sensor", "config_error", "Configuration Error", NULL, "state/config/error", "{\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value[:255] if value else 'none' }}\"}"},
    {"sensor", "contract_version", "MQTT Contract", NULL, "state/contract_version", "{\"ent_cat\":\"diagnostic\"}"},
    {"text", "weather_payload", "Weather Payload", "set/weather", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "media_payload", "Media Payload", "set/media", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "media_power_payload", "Media Power Payload", "set/media/power", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "pages_payload", "Page Layout Payload", "set/pages", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "display_power_payload", "Display Settings Payload", "set/display_power", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "compatibility_payload", "Sync Compatibility Payload", "set/blueprint_info", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "clock_text", "Panel Clock Text", "set/clock", "state/clock", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "date_text", "Panel Date Text", "set/date", "state/date", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "named_screenshot", "Capture Named Screenshot", "cmd/screenshot", NULL, "{\"max\":31,\"min\":1,\"ptrn\":\"[A-Za-z0-9_-]{1,31}\",\"ret\":false}"},
    {"number", "dim_after", "Dim After", "set/display_power", "state/display_power", "{\"min\":0,\"max\":86400,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"s\",\"val_tpl\":\"{{ value_json.dim_after }}\",\"cmd_tpl\":\"{{ {'dim_after': value | int, 'off_after': state_attr(this.entity_id, 'off_after') | int(600), 'dim_percent': state_attr(this.entity_id, 'dim_percent') | int(20)} | tojson }}\",\"ent_cat\":\"config\",\"ret\":false,\"json_attr_t\":\"$state\"}"},
    {"number", "off_after", "Screen Off After", "set/display_power", "state/display_power", "{\"min\":0,\"max\":86400,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"s\",\"val_tpl\":\"{{ value_json.off_after }}\",\"cmd_tpl\":\"{{ {'dim_after': state_attr(this.entity_id, 'dim_after') | int(300), 'off_after': value | int, 'dim_percent': state_attr(this.entity_id, 'dim_percent') | int(20)} | tojson }}\",\"ent_cat\":\"config\",\"ret\":false,\"json_attr_t\":\"$state\"}"},
    {"number", "dim_percent", "Dim Brightness", "set/display_power", "state/display_power", "{\"min\":0,\"max\":100,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"%\",\"val_tpl\":\"{{ value_json.dim_percent }}\",\"cmd_tpl\":\"{{ {'dim_after': state_attr(this.entity_id, 'dim_after') | int(300), 'off_after': state_attr(this.entity_id, 'off_after') | int(600), 'dim_percent': value | int} | tojson }}\",\"ent_cat\":\"config\",\"ret\":false,\"json_attr_t\":\"$state\"}"},
    {"button", "media_previous", "Media Previous", "cmd/media/previous", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"button", "media_play_pause", "Media Play Pause", "cmd/media/play_pause", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"button", "media_next", "Media Next", "cmd/media/next", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"button", "media_power_off", "Media Power Off", "cmd/media/power_off", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"button", "media_volume_down", "Media Volume Down", "cmd/media/volume_down", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"button", "media_volume_up", "Media Volume Up", "cmd/media/volume_up", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"number", "media_volume", "Media Volume", "cmd/media/volume", "state/media", "{\"min\":0,\"max\":100,\"step\":1,\"unit_of_meas\":\"%\",\"val_tpl\":\"{{ (value_json.volume_level | default(0, true) * 100) | round(0) }}\",\"cmd_tpl\":\"{{ value | int }}\",\"ret\":false}"},
    {"button", "media_favorite1", "Media Favourite 1", "cmd/media/favorite1", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "favorite1_label", "Favourite 1 Label", "set/media/favorite1/label", "state/media/favorite1/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "favorite1_icon", "Favourite 1 Icon", "set/media/favorite1/icon", "state/media/favorite1/icon", "{\"ops\":[\"none\",\"radio\",\"music\",\"album\",\"playlist\",\"podcast\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "button1_label", "Footer 1 Label", "set/button1/label", "state/button1/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "button1_input_state", "Footer 1 Input State", "set/button1/state", "state/button1/state", "{\"ops\":[\"on\",\"off\",\"unknown\",\"unavailable\",\"stateless\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "button1_action", "Footer 1 Last Action", NULL, "state/button1_action", "{\"ent_cat\":\"diagnostic\"}"},
    {"button", "media_favorite2", "Media Favourite 2", "cmd/media/favorite2", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "favorite2_label", "Favourite 2 Label", "set/media/favorite2/label", "state/media/favorite2/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "favorite2_icon", "Favourite 2 Icon", "set/media/favorite2/icon", "state/media/favorite2/icon", "{\"ops\":[\"none\",\"radio\",\"music\",\"album\",\"playlist\",\"podcast\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "button2_label", "Footer 2 Label", "set/button2/label", "state/button2/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "button2_input_state", "Footer 2 Input State", "set/button2/state", "state/button2/state", "{\"ops\":[\"on\",\"off\",\"unknown\",\"unavailable\",\"stateless\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "button2_action", "Footer 2 Last Action", NULL, "state/button2_action", "{\"ent_cat\":\"diagnostic\"}"},
    {"button", "media_favorite3", "Media Favourite 3", "cmd/media/favorite3", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "favorite3_label", "Favourite 3 Label", "set/media/favorite3/label", "state/media/favorite3/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "favorite3_icon", "Favourite 3 Icon", "set/media/favorite3/icon", "state/media/favorite3/icon", "{\"ops\":[\"none\",\"radio\",\"music\",\"album\",\"playlist\",\"podcast\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "button3_label", "Footer 3 Label", "set/button3/label", "state/button3/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "button3_input_state", "Footer 3 Input State", "set/button3/state", "state/button3/state", "{\"ops\":[\"on\",\"off\",\"unknown\",\"unavailable\",\"stateless\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "button3_action", "Footer 3 Last Action", NULL, "state/button3_action", "{\"ent_cat\":\"diagnostic\"}"},
    {"button", "media_favorite4", "Media Favourite 4", "cmd/media/favorite4", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "favorite4_label", "Favourite 4 Label", "set/media/favorite4/label", "state/media/favorite4/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "favorite4_icon", "Favourite 4 Icon", "set/media/favorite4/icon", "state/media/favorite4/icon", "{\"ops\":[\"none\",\"radio\",\"music\",\"album\",\"playlist\",\"podcast\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "button4_label", "Footer 4 Label", "set/button4/label", "state/button4/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "button4_input_state", "Footer 4 Input State", "set/button4/state", "state/button4/state", "{\"ops\":[\"on\",\"off\",\"unknown\",\"unavailable\",\"stateless\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "button4_action", "Footer 4 Last Action", NULL, "state/button4_action", "{\"ent_cat\":\"diagnostic\"}"},
    {"button", "media_favorite5", "Media Favourite 5", "cmd/media/favorite5", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "favorite5_label", "Favourite 5 Label", "set/media/favorite5/label", "state/media/favorite5/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "favorite5_icon", "Favourite 5 Icon", "set/media/favorite5/icon", "state/media/favorite5/icon", "{\"ops\":[\"none\",\"radio\",\"music\",\"album\",\"playlist\",\"podcast\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "button5_label", "Footer 5 Label", "set/button5/label", "state/button5/label", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"select", "button5_input_state", "Footer 5 Input State", "set/button5/state", "state/button5/state", "{\"ops\":[\"on\",\"off\",\"unknown\",\"unavailable\",\"stateless\"],\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "button5_action", "Footer 5 Last Action", NULL, "state/button5_action", "{\"ent_cat\":\"diagnostic\"}"},
    {"button", "grid1", "Buttons Page 1 Press", "cmd/grid1", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid1_payload", "Buttons Page 1 Payload", "set/grid/1", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid1_state", "Buttons Page 1 State", NULL, "state/grid/1", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"button", "grid2", "Buttons Page 2 Press", "cmd/grid2", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid2_payload", "Buttons Page 2 Payload", "set/grid/2", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid2_state", "Buttons Page 2 State", NULL, "state/grid/2", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"button", "grid3", "Buttons Page 3 Press", "cmd/grid3", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid3_payload", "Buttons Page 3 Payload", "set/grid/3", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid3_state", "Buttons Page 3 State", NULL, "state/grid/3", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"button", "grid4", "Buttons Page 4 Press", "cmd/grid4", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid4_payload", "Buttons Page 4 Payload", "set/grid/4", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid4_state", "Buttons Page 4 State", NULL, "state/grid/4", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"button", "grid5", "Buttons Page 5 Press", "cmd/grid5", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid5_payload", "Buttons Page 5 Payload", "set/grid/5", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid5_state", "Buttons Page 5 State", NULL, "state/grid/5", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"button", "grid6", "Buttons Page 6 Press", "cmd/grid6", NULL, "{\"pl_prs\":\"press\",\"ret\":false}"},
    {"text", "grid6_payload", "Buttons Page 6 Payload", "set/grid/6", NULL, "{\"max\":255,\"ent_cat\":\"config\",\"ret\":false}"},
    {"sensor", "grid6_state", "Buttons Page 6 State", NULL, "state/grid/6", "{\"val_tpl\":\"{{ value_json.state }}\",\"ent_cat\":\"diagnostic\",\"json_attr_t\":\"$state\"}"},
    {"text", "chip1_text", "Chip 1 Text", "set/chip1", "state/chip1", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip1_color_input", "Chip 1 Color", "set/chip1/color", "state/chip1/color", "{\"max\":7,\"ptrn\":\"#[0-9A-Fa-f]{6}\",\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip2_text", "Chip 2 Text", "set/chip2", "state/chip2", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip2_color_input", "Chip 2 Color", "set/chip2/color", "state/chip2/color", "{\"max\":7,\"ptrn\":\"#[0-9A-Fa-f]{6}\",\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip3_text", "Chip 3 Text", "set/chip3", "state/chip3", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip3_color_input", "Chip 3 Color", "set/chip3/color", "state/chip3/color", "{\"max\":7,\"ptrn\":\"#[0-9A-Fa-f]{6}\",\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip4_text", "Chip 4 Text", "set/chip4", "state/chip4", "{\"max\":48,\"ent_cat\":\"config\",\"ret\":false}"},
    {"text", "chip4_color_input", "Chip 4 Color", "set/chip4/color", "state/chip4/color", "{\"max\":7,\"ptrn\":\"#[0-9A-Fa-f]{6}\",\"ent_cat\":\"config\",\"ret\":false}"},
};

static esp_err_t publish_endpoint_controls(esp_mqtt_client_handle_t client) {
    for (size_t i = 0; i < sizeof(s_endpoints) / sizeof(s_endpoints[0]); ++i) {
        const discovery_endpoint_t *endpoint = &s_endpoints[i];
        DISCOVERY_CHECK(publish_discovery_options(client, endpoint->component, endpoint->id,
            endpoint->name, endpoint->command, endpoint->state, endpoint->options));
    }
    return ESP_OK;
}

esp_err_t ha_discovery_publish_all(esp_mqtt_client_handle_t client) {
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    DISCOVERY_CHECK(publish_endpoint_controls(client));
    DISCOVERY_CHECK(publish_discovery(client,
                                      "sensor",
                                      "weather",
                                      "Weather Summary",
                                      NULL,
                                      "state/weather"));
    DISCOVERY_CHECK(publish_discovery(client,
                                      "sensor",
                                      "media",
                                      "Now Playing",
                                      NULL,
                                      "state/media"));
    DISCOVERY_CHECK(publish_discovery(client, "text", "name", "Panel Name", "set/name", "state/name"));
    DISCOVERY_CHECK(publish_discovery(client, "sensor", "hostname", "Panel Hostname", NULL, "state/hostname"));
    DISCOVERY_CHECK(publish_discovery(client, "sensor", "clock", "Panel Time", NULL, "state/clock"));
    DISCOVERY_CHECK(publish_discovery(client, "sensor", "date", "Panel Date", NULL, "state/date"));
    for (int i = 1; i <= 4; ++i) {
        char object_id[32];
        char name[32];
        char state_suffix[32];
        snprintf(object_id, sizeof(object_id), "chip%d", i);
        snprintf(name, sizeof(name), "Panel Chip %d", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/chip%d", i);
        DISCOVERY_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
        snprintf(object_id, sizeof(object_id), "chip%d_color", i);
        snprintf(name, sizeof(name), "Panel Chip %d Color", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/chip%d/color", i);
        DISCOVERY_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
    }
    DISCOVERY_CHECK(publish_discovery(client, "button", "sync", "Sync Panel", "cmd/sync", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "button", "wake", "Wake Panel", "cmd/wake", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "switch", "ap", "Setup Access Point", "cmd/config/ap", "state/config/ap"));
    DISCOVERY_CHECK(publish_discovery(client, "text", "screenshot_token", "Screenshot Token", "cmd/config/screenshot_token", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "sensor", "screenshot_status", "Screenshot Status", NULL, "state/screenshot"));
    DISCOVERY_CHECK(publish_discovery(client, "button", "screenshot", "Capture Screenshot", "cmd/screenshot", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "button", "factory_reset", "Factory Reset and Reboot", "cmd/config/factory_reset", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "button", "reboot", "Reboot Panel", "cmd/config/reboot", NULL));
    DISCOVERY_CHECK(publish_discovery(client, "text", "base_topic", "Panel MQTT Topic", "cmd/config/base_topic", "state/config/base_topic"));
    DISCOVERY_CHECK(ha_discovery_publish_page_options(client));
    DISCOVERY_CHECK(publish_discovery(client, "text", "update_manifest", "Panel Update Manifest URL", "cmd/update", NULL));

    for (int i = 1; i <= 5; ++i) {
        char object_id[32];
        char name[32];
        char command_suffix[32];
        char state_suffix[32];
        snprintf(object_id, sizeof(object_id), "button%d", i);
        snprintf(name, sizeof(name), "Panel Button %d", i);
        snprintf(command_suffix, sizeof(command_suffix), "cmd/button%d", i);
        DISCOVERY_CHECK(publish_discovery(client, "button", object_id, name, command_suffix, NULL));

        snprintf(object_id, sizeof(object_id), "button%d_state", i);
        snprintf(name, sizeof(name), "Panel Button %d State", i);
        snprintf(state_suffix, sizeof(state_suffix), "state/button%d/state", i);
        DISCOVERY_CHECK(publish_discovery(client, "sensor", object_id, name, NULL, state_suffix));
    }

    DISCOVERY_CHECK(publish_device_diagnostic(client, "ip", "IP Address", "ip", NULL, NULL, "mdi:ip-network"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "ipv6_global", "IPv6 Address", "ipv6_global", NULL, NULL, "mdi:ip-network"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "ipv6_linklocal", "IPv6 Link-local Address", "ipv6_linklocal", NULL, NULL, "mdi:ip-network"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "mac", "Wi-Fi MAC", "mac", NULL, NULL, "mdi:network-outline"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "firmware", "Firmware Version", "firmware_version", NULL, NULL, "mdi:chip"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "blueprint", "Blueprint Version", "blueprint_version", NULL, NULL, "mdi:script-text"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "firmware_contract", "Firmware Contract", "firmware_contract_version", NULL, NULL, "mdi:file-document-check"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "blueprint_contract", "Blueprint Contract", "blueprint_contract_version", NULL, NULL, "mdi:file-document-check"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "contract_match", "Contract Compatible", "contract_match", NULL, NULL, "mdi:check-decagram"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "partition", "Running Partition", "app_partition", NULL, NULL, "mdi:memory"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "reset_reason", "Last Reset Reason", "reset_reason", NULL, NULL, "mdi:restart"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "ssid", "Wi-Fi SSID", "ssid", NULL, NULL, "mdi:wifi"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "bssid", "Wi-Fi BSSID", "wifi_bssid", NULL, NULL, "mdi:wifi-cog"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "channel", "Wi-Fi Channel", "wifi_channel", NULL, NULL, "mdi:wifi"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "rssi", "Wi-Fi Signal", "wifi_rssi", "dBm", "signal_strength", "mdi:wifi"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "uptime", "Uptime", "uptime_s", "s", "duration", "mdi:timer-outline"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "heap", "Free Heap", "free_heap", "B", "data_size", "mdi:memory"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "min_heap", "Minimum Free Heap", "min_free_heap", "B", "data_size", "mdi:memory"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "psram", "Free PSRAM", "free_psram", "B", "data_size", "mdi:memory"));
    DISCOVERY_CHECK(publish_device_diagnostic(client, "min_psram", "Minimum Free PSRAM", "min_free_psram", "B", "data_size", "mdi:memory"));
    return ESP_OK;
}
