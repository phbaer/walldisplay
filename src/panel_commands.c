#include "walldisplay/panel_commands.h"
#include "walldisplay/app_config.h"
#include "walldisplay/mqtt_app.h"
#include <stdio.h>
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
