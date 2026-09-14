#pragma once

#include "esp_err.h"

esp_err_t wifi_manager_start(void);
/** Apply the configured panel hostname to the station network interface. */
esp_err_t wifi_manager_set_hostname(const char *hostname);
/** Persist a human-readable label and apply its generated station hostname. */
esp_err_t wifi_manager_set_display_name(const char *display_name);
