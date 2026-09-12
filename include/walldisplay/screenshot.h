#pragma once

#include "esp_err.h"
#include "walldisplay/display_board.h"

#include <stdbool.h>

typedef esp_err_t (*screenshot_status_cb_t)(const char *topic_suffix, const char *payload, bool retain);

/* Mount screenshot storage and start the read-only local HTTP endpoint. */
esp_err_t screenshot_init(const display_board_handle_t *board, screenshot_status_cb_t status_callback);

/* Queue a capture of the currently displayed frame. */
esp_err_t screenshot_request(void);
esp_err_t screenshot_request_named(const char *name);
