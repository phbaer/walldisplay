#include "walldisplay/panel_commands.h"
#include "walldisplay/app_config.h"
#include "walldisplay/mqtt_app.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static esp_err_t publish_media_payload(const char *command, const char *type,
                                       const char *id, size_t offset) {
    if (command == NULL || (type != NULL && strlen(type) > 64) ||
        (id != NULL && strlen(id) > 256)) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    if ((type != NULL && type[0] != '\0' && cJSON_AddStringToObject(root, "media_content_type", type) == NULL) ||
        (id != NULL && id[0] != '\0' && cJSON_AddStringToObject(root, "media_content_id", id) == NULL) ||
        (strcmp(command, "browse") == 0 && cJSON_AddNumberToObject(root, "offset", (double)offset) == NULL)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) return ESP_ERR_NO_MEM;
    const size_t topic_capacity = APP_TOPIC_MAX_LEN + 48;
    char *topic = malloc(topic_capacity);
    if (topic == NULL) { free(payload); return ESP_ERR_NO_MEM; }
    snprintf(topic, topic_capacity, "%s/cmd/media/%s", app_config_get()->base_topic, command);
    esp_err_t result = mqtt_app_publish_async(topic, payload, false);
    free(topic);
    free(payload);
    return result;
}

esp_err_t panel_commands_media_browse(const char *type, const char *id, size_t offset) {
    return publish_media_payload("browse", type, id, offset);
}

esp_err_t panel_commands_media_play(const char *type, const char *id) {
    if (type == NULL || type[0] == '\0' || id == NULL || id[0] == '\0') return ESP_ERR_INVALID_ARG;
    return publish_media_payload("play", type, id, 0);
}
esp_err_t panel_commands_handle(ui_action_t action, void *context) {
    (void)context;
    char suffix[40], topic[APP_TOPIC_MAX_LEN + 48], payload[8] = "press";
    static const char *const media[] = {"previous", "play_pause", "next", "power_off", "volume_down", "volume_up"};
    if (action.kind == UI_ACTION_FOOTER || action.kind == UI_ACTION_GRID || action.kind == UI_ACTION_FAVORITE) {
        int maximum = action.kind == UI_ACTION_GRID ? 6 : 5;
        if (action.value < 0 || action.value >= maximum) return ESP_ERR_INVALID_ARG;
        snprintf(suffix, sizeof(suffix), action.kind == UI_ACTION_FOOTER ? "button%d" :
            action.kind == UI_ACTION_GRID ? "grid%d" : "media/favorite%d", action.value + 1);
        if (action.kind == UI_ACTION_FOOTER) snprintf(payload, sizeof(payload), "toggle");
    } else if (action.kind == UI_ACTION_VOLUME) {
        if (action.value < 0 || action.value > 100) return ESP_ERR_INVALID_ARG;
        snprintf(suffix, sizeof(suffix), "media/volume");
        snprintf(payload, sizeof(payload), "%d", action.value);
    } else if (action.kind >= UI_ACTION_PREVIOUS && action.kind <= UI_ACTION_VOLUME_UP) {
        snprintf(suffix, sizeof(suffix), "media/%s", media[action.kind - UI_ACTION_PREVIOUS]);
    } else return ESP_ERR_INVALID_ARG;
    snprintf(topic, sizeof(topic), "%s/cmd/%s", app_config_get()->base_topic, suffix);
    esp_err_t result = mqtt_app_publish_async(topic, payload, false);
    if (result == ESP_OK && action.kind == UI_ACTION_FOOTER) {
        snprintf(topic, sizeof(topic), "%s/state/button%d_action", app_config_get()->base_topic, action.value + 1);
        result = mqtt_app_publish_async(topic, "toggle", true);
    }
    return result;
}
