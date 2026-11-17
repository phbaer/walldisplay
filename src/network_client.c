#include "walldisplay/network_client.h"

#include "esp_transport_ssl.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static esp_netif_t *station(void) {
    return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}

esp_err_t network_get_ip6_address(esp_netif_t *netif, esp_ip6_addr_t *address) {
    if (netif == NULL || address == NULL) return ESP_ERR_INVALID_ARG;
    esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    const int count = esp_netif_get_all_preferred_ip6(netif, addresses);
    /* A ULA is useful for LAN access even when no public prefix is advertised. */
    for (int pass = 0; pass < 2; ++pass) {
        const esp_ip6_addr_type_t wanted = pass == 0 ? ESP_IP6_ADDR_IS_UNIQUE_LOCAL : ESP_IP6_ADDR_IS_GLOBAL;
        for (int i = 0; i < count; ++i) {
            if (esp_netif_ip6_get_addr_type(&addresses[i]) == wanted) {
                *address = addresses[i];
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

bool network_station_has_ipv4(void) {
    esp_netif_t *netif = station();
    esp_netif_ip_info_t info = {0};
    return netif != NULL && esp_netif_is_netif_up(netif) &&
           esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0;
}

bool network_station_ready(void) {
    esp_ip6_addr_t address;
    return network_station_has_ipv4() || network_get_ip6_address(station(), &address) == ESP_OK;
}

esp_err_t network_station_http_host(char *host, size_t size) {
    if (host == NULL || size == 0) return ESP_ERR_INVALID_ARG;
    host[0] = '\0';
    int length;
    if (network_station_has_ipv4()) {
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(station(), &info) != ESP_OK) return ESP_ERR_INVALID_STATE;
        length = snprintf(host, size, IPSTR, IP2STR(&info.ip));
    } else {
        esp_ip6_addr_t address;
        if (network_get_ip6_address(station(), &address) != ESP_OK) return ESP_ERR_INVALID_STATE;
        length = snprintf(host, size, "[" IPV6STR "]", IPV62STR(address));
    }
    return length >= 0 && (size_t)length < size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static network_family_id_t literal_family(const char *uri) {
    const char *host = uri == NULL ? NULL : strstr(uri, "://");
    if (host == NULL) return NETWORK_FAMILY_UNKNOWN;
    host += 3;
    const size_t authority = strcspn(host, "/?#");
    /* Skip optional userinfo; the host follows the last '@' of the authority. */
    for (size_t i = authority; i > 0; --i) {
        if (host[i - 1] == '@') { host += i; break; }
    }
    if (host[0] == '[') return NETWORK_FAMILY_IPV6;
    const size_t length = strcspn(host, ":/?#");
    if (length == 0) return NETWORK_FAMILY_UNKNOWN;
    /* Four dot-separated decimal fields; anything else is resolved as a name. */
    unsigned dots = 0;
    for (size_t i = 0; i < length; ++i) {
        if (host[i] == '.') ++dots;
        else if (host[i] < '0' || host[i] > '9') return NETWORK_FAMILY_UNKNOWN;
    }
    return dots == 3 ? NETWORK_FAMILY_IPV4 : NETWORK_FAMILY_UNKNOWN;
}

static network_family_id_t other_family(network_family_id_t family) {
    return family == NETWORK_FAMILY_IPV4 ? NETWORK_FAMILY_IPV6 : NETWORK_FAMILY_IPV4;
}

network_family_id_t network_family_select(network_family_t *state, const char *uri) {
    const network_family_id_t literal = literal_family(uri);
    const bool ipv4 = network_station_has_ipv4();
    esp_ip6_addr_t address;
    const bool ipv6 = network_get_ip6_address(station(), &address) == ESP_OK;
    /* Prefer IPv4 on existing LANs, so broken or unscoped AAAA records do not
     * regress working deployments. */
    network_family_id_t family = ipv4 || !ipv6 ? NETWORK_FAMILY_IPV4 : NETWORK_FAMILY_IPV6;
    if (literal != NETWORK_FAMILY_UNKNOWN) {
        family = literal;
    } else if (state->failed && !state->connected && state->current != NETWORK_FAMILY_UNKNOWN) {
        family = other_family((network_family_id_t)state->current);
    } else if ((state->good == NETWORK_FAMILY_IPV4 && ipv4) || (state->good == NETWORK_FAMILY_IPV6 && ipv6)) {
        family = (network_family_id_t)state->good;
    }
    state->literal = literal != NETWORK_FAMILY_UNKNOWN;
    state->current = (uint8_t)family;
    state->failed = false;
    state->connected = false;
    return family;
}

void network_family_result(network_family_t *state, bool connected) {
    if (state->current == NETWORK_FAMILY_UNKNOWN) return;
    if (connected) {
        state->connected = true;
        state->failed = false;
        state->good = state->current;
    } else if (!state->connected) {
        state->failed = true;
    }
}

bool network_family_has_alternative(const network_family_t *state) {
    return !state->literal;
}

/* HTTP peers share one remembered family: artwork and release hosts are
 * reached over the same station, and a wrong guess costs one attempt. */
static _Atomic uint8_t s_http_good_family;

esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client) {
    if (config == NULL || config->url == NULL || client == NULL) return ESP_ERR_INVALID_ARG;
    *client = NULL;
    esp_err_t err = ESP_FAIL;
    network_family_t state = {.good = atomic_load(&s_http_good_family)};
    for (int attempt = 0; attempt < 2; ++attempt) {
        esp_http_client_config_t candidate = *config;
        candidate.addr_type = network_family_select(&state, config->url) == NETWORK_FAMILY_IPV4
                                  ? HTTP_ADDR_TYPE_INET : HTTP_ADDR_TYPE_INET6;
        esp_http_client_handle_t handle = esp_http_client_init(&candidate);
        if (handle == NULL) return ESP_ERR_NO_MEM;
        err = range ? esp_http_client_set_header(handle, "Range", range) : ESP_OK;
        if (err != ESP_OK) {
            esp_http_client_cleanup(handle);
            return err;
        }
        err = esp_http_client_open(handle, 0);
        network_family_result(&state, err == ESP_OK);
        if (err == ESP_OK) {
            atomic_store(&s_http_good_family, state.good);
            *client = handle;
            return ESP_OK;
        }
        esp_http_client_cleanup(handle);
        if (err == ESP_ERR_NO_MEM || !network_family_has_alternative(&state)) break;
    }
    return err;
}

void network_mqtt_before_connect(esp_mqtt_client_handle_t client, const char *uri, network_family_t *state) {
    const bool tls = strncmp(uri, "mqtts://", 8) == 0;
    const network_family_id_t family = network_family_select(state, uri);
    esp_transport_handle_t transport = esp_mqtt_client_get_transport(client, tls ? "mqtts" : "mqtt");
    if (transport != NULL) {
        /* ESP-IDF's TCP and SSL transports share the ESP-TLS configuration. */
        esp_transport_ssl_set_addr_family(transport, family == NETWORK_FAMILY_IPV4
                                         ? ESP_TLS_AF_INET : ESP_TLS_AF_INET6);
    }
}
