#include "walldisplay/network_client.h"

#include "esp_timer.h"
#include "esp_transport_ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

/* Split a URI into the destination key "host:port" and its literal family.
 * Returns false for URIs that are not http, https, mqtt, or mqtts. */
static bool parse_destination(const char *uri, char *key, size_t key_size, network_family_id_t *literal) {
    *literal = NETWORK_FAMILY_UNKNOWN;
    const char *scheme_end = uri == NULL ? NULL : strstr(uri, "://");
    if (scheme_end == NULL || scheme_end == uri) return false;
    const size_t scheme_length = (size_t)(scheme_end - uri);
    unsigned port = 0;
    if (scheme_length == 4 && strncmp(uri, "http", 4) == 0) port = 80;
    else if (scheme_length == 5 && strncmp(uri, "https", 5) == 0) port = 443;
    else if (scheme_length == 4 && strncmp(uri, "mqtt", 4) == 0) port = 1883;
    else if (scheme_length == 5 && strncmp(uri, "mqtts", 5) == 0) port = 8883;
    else return false;

    const char *authority = scheme_end + 3;
    const size_t authority_length = strcspn(authority, "/?#");
    /* Skip optional userinfo; the host follows the last '@' of the authority. */
    for (size_t i = authority_length; i > 0; --i) {
        if (authority[i - 1] == '@') { authority += i; break; }
    }
    const size_t remaining = authority_length - (size_t)(authority - (scheme_end + 3));
    const bool bracketed = authority[0] == '[';
    size_t host_length;
    if (bracketed) {
        const char *close = memchr(authority, ']', remaining);
        if (close == NULL) return false;
        host_length = (size_t)(close - authority) + 1;
    } else {
        host_length = strcspn(authority, ":/?#");
    }
    if (host_length == 0 || host_length >= 64) return false;

    char host[64];
    for (size_t i = 0; i < host_length; ++i) {
        const char c = authority[i];
        host[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    host[host_length] = '\0';

    const char *after = authority + host_length;
    if (after < authority + remaining && *after == ':') {
        unsigned value = 0;
        size_t digits = 0;
        for (const char *p = after + 1; p < authority + remaining; ++p, ++digits) {
            if (*p < '0' || *p > '9' || value > 6553) return false;
            value = value * 10 + (unsigned)(*p - '0');
        }
        if (digits == 0 || value == 0 || value > 65535) return false;
        port = value;
    } else if (after < authority + remaining) {
        return false;
    }

    if (bracketed) {
        *literal = NETWORK_FAMILY_IPV6;
    } else {
        /* Four dot-separated decimal fields; anything else is resolved as a name. */
        unsigned dots = 0;
        bool numeric = true;
        for (size_t i = 0; i < host_length; ++i) {
            if (host[i] == '.') ++dots;
            else if (host[i] < '0' || host[i] > '9') numeric = false;
        }
        if (numeric && dots == 3) *literal = NETWORK_FAMILY_IPV4;
    }
    return snprintf(key, key_size, "%s:%u", host, port) < (int)key_size;
}

static network_family_id_t other_family(network_family_id_t family) {
    return family == NETWORK_FAMILY_IPV4 ? NETWORK_FAMILY_IPV6 : NETWORK_FAMILY_IPV4;
}

/* Per-destination memory. A small table with least-recently-used replacement
 * is enough: the panel talks to a broker, a release host, and a few artwork
 * hosts. Entries expire, so a family that stops working is retried later. */
#define FAMILY_MEMORY_SLOTS 4
#define FAMILY_MEMORY_TTL_US (10LL * 60LL * 1000LL * 1000LL)

typedef struct {
    bool used;
    char key[72];
    uint8_t family;
    int64_t stored_us;
    uint32_t last_used;
} family_memory_t;

static family_memory_t s_memory[FAMILY_MEMORY_SLOTS];
static uint32_t s_memory_tick;
static SemaphoreHandle_t s_memory_lock;

esp_err_t network_client_init(void) {
    if (s_memory_lock == NULL) {
        s_memory_lock = xSemaphoreCreateMutex();
        if (s_memory_lock == NULL) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static network_family_id_t memory_get(const char *key) {
    network_family_id_t family = NETWORK_FAMILY_UNKNOWN;
    if (s_memory_lock == NULL) return family;
    xSemaphoreTake(s_memory_lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < FAMILY_MEMORY_SLOTS; ++i) {
        family_memory_t *entry = &s_memory[i];
        if (entry->used && strcmp(entry->key, key) == 0) {
            if (now - entry->stored_us < FAMILY_MEMORY_TTL_US) {
                family = (network_family_id_t)entry->family;
                entry->last_used = ++s_memory_tick;
            } else {
                entry->used = false;
            }
            break;
        }
    }
    xSemaphoreGive(s_memory_lock);
    return family;
}

static void memory_set(const char *key, network_family_id_t family) {
    if (s_memory_lock == NULL) return;
    xSemaphoreTake(s_memory_lock, portMAX_DELAY);
    family_memory_t *target = NULL;
    for (size_t i = 0; i < FAMILY_MEMORY_SLOTS && target == NULL; ++i) {
        if (s_memory[i].used && strcmp(s_memory[i].key, key) == 0) target = &s_memory[i];
    }
    for (size_t i = 0; i < FAMILY_MEMORY_SLOTS && target == NULL; ++i) {
        if (!s_memory[i].used) target = &s_memory[i];
    }
    if (target == NULL) {
        target = &s_memory[0];
        for (size_t i = 1; i < FAMILY_MEMORY_SLOTS; ++i) {
            if (s_memory[i].last_used < target->last_used) target = &s_memory[i];
        }
    }
    target->used = true;
    snprintf(target->key, sizeof(target->key), "%s", key);
    target->family = (uint8_t)family;
    target->stored_us = esp_timer_get_time();
    target->last_used = ++s_memory_tick;
    xSemaphoreGive(s_memory_lock);
}

static void memory_forget(const char *key) {
    if (s_memory_lock == NULL) return;
    xSemaphoreTake(s_memory_lock, portMAX_DELAY);
    for (size_t i = 0; i < FAMILY_MEMORY_SLOTS; ++i) {
        if (s_memory[i].used && strcmp(s_memory[i].key, key) == 0) s_memory[i].used = false;
    }
    xSemaphoreGive(s_memory_lock);
}

network_family_id_t network_family_select(network_family_t *state, const char *uri) {
    char key[72];
    network_family_id_t literal = NETWORK_FAMILY_UNKNOWN;
    const bool keyed = parse_destination(uri, key, sizeof(key), &literal);
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
    } else if (state->remember && keyed) {
        const network_family_id_t remembered = memory_get(key);
        if ((remembered == NETWORK_FAMILY_IPV4 && ipv4) || (remembered == NETWORK_FAMILY_IPV6 && ipv6)) {
            family = remembered;
        }
    }
    state->literal = literal != NETWORK_FAMILY_UNKNOWN;
    state->current = (uint8_t)family;
    state->failed = false;
    state->connected = false;
    return family;
}

void network_family_result(network_family_t *state, const char *uri, bool connected) {
    if (state->current == NETWORK_FAMILY_UNKNOWN) return;
    if (!state->remember || state->literal) {
        if (connected) { state->connected = true; state->failed = false; }
        else if (!state->connected) state->failed = true;
        return;
    }
    char key[72];
    network_family_id_t literal = NETWORK_FAMILY_UNKNOWN;
    if (!parse_destination(uri, key, sizeof(key), &literal)) return;
    if (connected) {
        state->connected = true;
        state->failed = false;
        memory_set(key, (network_family_id_t)state->current);
    } else if (!state->connected) {
        state->failed = true;
        memory_forget(key);
    }
}

bool network_family_has_alternative(const network_family_t *state) {
    return !state->literal;
}

esp_err_t network_http_open(const esp_http_client_config_t *config, const char *range,
                            esp_http_client_handle_t *client) {
    if (config == NULL || config->url == NULL || client == NULL) return ESP_ERR_INVALID_ARG;
    *client = NULL;
    esp_err_t err = ESP_FAIL;
    network_family_t state = {.remember = true};
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
        network_family_result(&state, config->url, err == ESP_OK);
        if (err == ESP_OK) {
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
