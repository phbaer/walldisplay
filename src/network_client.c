#include "walldisplay/network_client.h"

#include "esp_transport_ssl.h"
#include "sdkconfig.h"
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

static bool ipv6_literal(const char *uri) {
    const char *authority = strstr(uri, "://");
    return authority != NULL && authority[3] == '[';
}

static esp_http_client_addr_type_t address_family(const char *uri, bool alternate) {
    /* Literals have only one meaningful family. Prefer IPv4 on existing LANs,
     * so broken/unscoped AAAA records do not regress working deployments. */
    if (ipv6_literal(uri)) return HTTP_ADDR_TYPE_INET6;
    const bool ipv4 = network_station_has_ipv4();
    return ipv4 != alternate ? HTTP_ADDR_TYPE_INET : HTTP_ADDR_TYPE_INET6;
}

esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client) {
    if (config == NULL || config->url == NULL || client == NULL) return ESP_ERR_INVALID_ARG;
    *client = NULL;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < (ipv6_literal(config->url) ? 1 : 2); ++attempt) {
        esp_http_client_config_t candidate = *config;
        candidate.addr_type = address_family(config->url, attempt != 0);
        esp_http_client_handle_t handle = esp_http_client_init(&candidate);
        if (handle == NULL) return ESP_ERR_NO_MEM;
        err = range ? esp_http_client_set_header(handle, "Range", range) : ESP_OK;
        if (err != ESP_OK) {
            esp_http_client_cleanup(handle);
            return err;
        }
        err = esp_http_client_open(handle, 0);
        if (err == ESP_OK) {
            *client = handle;
            return ESP_OK;
        }
        esp_http_client_cleanup(handle);
        if (err == ESP_ERR_NO_MEM) break;
    }
    return err;
}

void network_mqtt_before_connect(esp_mqtt_client_handle_t client, const char *uri, bool alternate) {
    const bool tls = strncmp(uri, "mqtts://", 8) == 0;
    esp_transport_handle_t transport = esp_mqtt_client_get_transport(client, tls ? "mqtts" : "mqtt");
    if (transport != NULL) {
        /* ESP-IDF's TCP and SSL transports share the ESP-TLS configuration. */
        esp_transport_ssl_set_addr_family(transport, address_family(uri, alternate) == HTTP_ADDR_TYPE_INET
                                         ? ESP_TLS_AF_INET : ESP_TLS_AF_INET6);
    }
}
