#pragma once

#include <stdbool.h>
#include <stdint.h>
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

typedef enum {
    NETWORK_FAMILY_UNKNOWN = 0,
    NETWORK_FAMILY_IPV4,
    NETWORK_FAMILY_IPV6,
} network_family_id_t;

/** Address-family choice for one logical peer. Zero-initialize before use.
 * The family that last connected is kept for later attempts, so a dropped
 * session or a later request starts where the previous one succeeded. Only a
 * connection attempt that never connected moves the next attempt to the other
 * family. IPv4 and bracketed IPv6 literals always use their own family. */
typedef struct {
    uint8_t good;      /* network_family_id_t that last connected */
    uint8_t current;   /* network_family_id_t of the running attempt */
    bool failed;       /* the running attempt failed before connecting */
    bool connected;    /* the running attempt connected */
    bool literal;      /* the URI allows only one family */
} network_family_t;

/** Start a connection attempt to uri and return the family to use. */
network_family_id_t network_family_select(network_family_t *state, const char *uri);
/** Report the outcome of the attempt started by network_family_select().
 * A failure reported after the attempt connected is a dropped session and
 * does not change the family. Repeated reports for one attempt are harmless. */
void network_family_result(network_family_t *state, bool connected);
/** False when the last selected URI is an address literal. */
bool network_family_has_alternative(const network_family_t *state);

/** Open a GET, retrying the other address family on connection failure.
 * The family that last connected is tried first on later calls. The caller
 * owns the returned client on success. TLS configuration and the URL hostname
 * are preserved. Optional Range is applied to both attempts.
 */
esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client);
/** Call during MQTT_EVENT_BEFORE_CONNECT, for mqtt:// or mqtts://. Report
 * MQTT_EVENT_CONNECTED and transport errors with network_family_result(). */
void network_mqtt_before_connect(esp_mqtt_client_handle_t client, const char *uri, network_family_t *state);

#ifdef __cplusplus
}
#endif
