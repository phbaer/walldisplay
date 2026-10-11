#pragma once

#include <stdbool.h>
#include "esp_netif.h"
#include "esp_http_client.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Preferred station ULA, otherwise global IPv6; excludes link-local addresses. */
esp_err_t network_get_ip6_address(esp_netif_t *netif, esp_ip6_addr_t *address);
bool network_station_has_ipv4(void);
bool network_station_ready(void);
/** IPv4 or bracketed IPv6 host suitable for an HTTP URL. */
esp_err_t network_station_http_host(char *host, size_t size);
/** Open a GET, retrying the other address family on connection failure.
 * The caller owns the returned client on success. TLS configuration and the
 * URL hostname are preserved. Optional Range is applied to both attempts.
 */
esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client);
/** Call during MQTT_EVENT_BEFORE_CONNECT, for mqtt:// or mqtts://. */
void network_mqtt_before_connect(esp_mqtt_client_handle_t client, const char *uri, bool alternate);

#ifdef __cplusplus
}
#endif
