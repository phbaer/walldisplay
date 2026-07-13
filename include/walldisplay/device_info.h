#pragma once

#include "esp_err.h"

#include <stdbool.h>

typedef esp_err_t (*device_info_publish_cb_t)(const char *topic_suffix, const char *payload, bool retain);

esp_err_t device_info_init(device_info_publish_cb_t publish_cb);
esp_err_t device_info_publish(void);
bool device_info_set_blueprint_info(const char *version, const char *contract_version);
