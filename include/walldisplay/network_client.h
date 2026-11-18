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

/** Prepare the destination memory. Call once before the first connection. */
esp_err_t network_client_init(void);

/** Address-family state for one connection sequence (for example one HTTP
 * request with its retry, or one MQTT client). Zero-initialize before use.
 *
 * The family that last connected to a destination (host and port) is
 * remembered for ten minutes, separately for each destination. A destination
 * whose remembered family failed is forgotten, so the next attempt starts from
 * the default again. Set remember to false for probes that must not change
 * what later connections use. */
typedef struct {
    uint8_t current;   /* network_family_id_t of the running attempt */
    bool failed;       /* the running attempt failed before connecting */
    bool connected;    /* the running attempt connected */
    bool literal;      /* the URI is an address literal: one family only */
    bool remember;     /* read and update the per-destination memory */
} network_family_t;

/** Start a connection attempt to uri and return the family to use. Preference:
 * an address literal's own family; after a failed attempt the other family;
 * otherwise the remembered family if the station still has it; otherwise IPv4
 * when present, else IPv6. */
network_family_id_t network_family_select(network_family_t *state, const char *uri);
/** Report the outcome of the attempt started by network_family_select().
 * A failure reported after the attempt connected is a dropped session: it
 * does not forget the destination's family. Repeated reports are harmless. */
void network_family_result(network_family_t *state, const char *uri, bool connected);
/** False when the URI is an address literal and no other family can be tried. */
bool network_family_has_alternative(const network_family_t *state);

/** Open a GET, retrying the other address family on connection failure.
 * The family that last connected to the URL's destination is tried first. The
 * caller owns the returned client on success. TLS configuration and the URL
 * hostname are preserved. Optional Range is applied to both attempts.
 */
esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client);
/** Call during MQTT_EVENT_BEFORE_CONNECT, for mqtt:// or mqtts://. Report
 * MQTT_EVENT_CONNECTED and transport errors with network_family_result(). */
void network_mqtt_before_connect(esp_mqtt_client_handle_t client, const char *uri, network_family_t *state);

#ifdef __cplusplus
}
#endif
