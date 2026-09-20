#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t wifi_manager_start(void);
/** Apply the configured panel hostname to the station network interface. */
esp_err_t wifi_manager_set_hostname(const char *hostname);
/** Persist a human-readable label and apply its generated station hostname. */
esp_err_t wifi_manager_set_display_name(const char *display_name);
esp_err_t wifi_manager_start_ap(void);
esp_err_t wifi_manager_stop_ap(void);
bool wifi_manager_ap_active(void);
bool wifi_manager_station_ready(void);
esp_err_t wifi_manager_get_ap_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
