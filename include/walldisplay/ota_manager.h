#pragma once

#include "esp_err.h"

#include <stdbool.h>

typedef esp_err_t (*ota_status_publish_cb_t)(const char *topic_suffix,
                                             const char *payload,
                                             bool retain);

esp_err_t ota_manager_init(ota_status_publish_cb_t publish_cb);
esp_err_t ota_manager_request(const char *manifest_url);
esp_err_t ota_manager_mark_running_image_valid(void);
