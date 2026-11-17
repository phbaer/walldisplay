#pragma once

#include "esp_err.h"
#include <stdbool.h>
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

/* Used by the setup portal. */

/** Record setup-portal activity; restarts the idle timeout of an on-demand AP. */
void wifi_manager_ap_activity(void);
/** Connect the station to a candidate network without storing it. On failure
 * the previous station configuration is restored. On success the candidate
 * stays connected until wifi_manager_restore_station() or a reboot. */
esp_err_t wifi_manager_test_station(const char *ssid, const char *password);
/** Return the station to the configuration saved by the last successful
 * wifi_manager_test_station(). */
void wifi_manager_restore_station(void);
/** Try to connect to a broker with candidate settings. An empty URI passes. */
esp_err_t wifi_manager_test_mqtt(const char *uri, const char *username, const char *password,
                                 bool require_tls, const char *ca_certificate);
