#pragma once
#include "ui_actions.h"
#include <stddef.h>
esp_err_t panel_commands_media_browse(const char *media_content_type,
                                      const char *media_content_id, size_t offset);
esp_err_t panel_commands_media_play(const char *media_content_type,
                                    const char *media_content_id);
esp_err_t panel_commands_handle(ui_action_t action, void *context);
