#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    UI_ACTION_FOOTER, UI_ACTION_GRID, UI_ACTION_PREVIOUS, UI_ACTION_PLAY_PAUSE,
    UI_ACTION_NEXT, UI_ACTION_POWER_OFF, UI_ACTION_VOLUME_DOWN, UI_ACTION_VOLUME_UP,
    UI_ACTION_VOLUME, UI_ACTION_FAVORITE,
} ui_action_kind_t;
typedef struct { ui_action_kind_t kind; int value; } ui_action_t;
typedef esp_err_t (*ui_action_handler_t)(ui_action_t action, void *context);
void ui_actions_set_handler(ui_action_handler_t handler, void *context);
esp_err_t ui_actions_emit(ui_action_t action);
#ifdef __cplusplus
}
#endif
