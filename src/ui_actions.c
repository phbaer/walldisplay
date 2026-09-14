#include "walldisplay/ui_actions.h"
#include <stddef.h>
static ui_action_handler_t s_handler;
static void *s_context;
void ui_actions_set_handler(ui_action_handler_t handler, void *context) {
    s_handler = handler;
    s_context = context;
}
esp_err_t ui_actions_emit(ui_action_t action) {
    return s_handler != NULL ? s_handler(action, s_context) : ESP_ERR_INVALID_STATE;
}
