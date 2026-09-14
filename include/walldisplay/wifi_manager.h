#pragma once

#include "esp_err.h"

esp_err_t wifi_manager_start(void);
/** Apply the configured panel hostname to the station network interface. */
esp_err_t wifi_manager_set_hostname(const char *hostname);
