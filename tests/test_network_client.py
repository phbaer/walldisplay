"""Exercise actual firmware family fallback, TLS settings, and address policy."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_network_clients_and_station_addresses(tmp_path):
    headers = {
        'esp_netif.h': r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_NO_MEM 5
typedef struct { int unused; } esp_netif_t;
typedef struct { uint32_t addr; } ip4_t;
typedef struct { ip4_t ip; } esp_netif_ip_info_t;
typedef struct { unsigned addr[4]; } esp_ip6_addr_t;
typedef enum { ESP_IP6_ADDR_IS_LINK_LOCAL, ESP_IP6_ADDR_IS_GLOBAL, ESP_IP6_ADDR_IS_UNIQUE_LOCAL } esp_ip6_addr_type_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 10U, 0U, 0U, (unsigned)(ip)->addr
#define IPV6STR "%x:%x:%x:%x:%x:%x:%x:%x"
#define IPV62STR(ip) (ip).addr[0], (ip).addr[1], (ip).addr[2], (ip).addr[3], 0U, 0U, 0U, 1U
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
bool esp_netif_is_netif_up(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
int esp_netif_get_all_preferred_ip6(esp_netif_t *netif, esp_ip6_addr_t *addresses);
esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(const esp_ip6_addr_t *address);
''',
        'esp_http_client.h': r'''
#pragma once
#include "esp_netif.h"
typedef enum { HTTP_ADDR_TYPE_INET, HTTP_ADDR_TYPE_INET6 } esp_http_client_addr_type_t;
typedef struct { const char *url; int timeout_ms; const void *crt_bundle_attach; const char *cert_pem; bool skip_cert_common_name_check; esp_http_client_addr_type_t addr_type; } esp_http_client_config_t;
typedef struct client *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t handle, int length);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle);
''',
        'mqtt_client.h': r'''
#pragma once
#include "esp_netif.h"
typedef void *esp_mqtt_client_handle_t;
typedef void *esp_transport_handle_t;
esp_transport_handle_t esp_mqtt_client_get_transport(esp_mqtt_client_handle_t client, char *scheme);
''',
        'esp_transport_ssl.h': r'''
#pragma once
#include "mqtt_client.h"
typedef enum { ESP_TLS_AF_INET, ESP_TLS_AF_INET6 } esp_tls_addr_family_t;
void esp_transport_ssl_set_addr_family(esp_transport_handle_t transport, esp_tls_addr_family_t family);
''',
        'sdkconfig.h': '#define CONFIG_LWIP_IPV6_NUM_ADDRESSES 4\n',
    }
    for name, content in headers.items():
        (tmp_path / name).write_text(content)
    harness = r'''
#include "walldisplay/network_client.h"
#include "esp_transport_ssl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static esp_netif_t netif;
static bool present = true, up = true;
static unsigned ipv4;
static esp_ip6_addr_t addresses[4];
static int count;
static esp_ip6_addr_type_t types[4];
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(strcmp(key, "WIFI_STA_DEF") == 0); return present ? &netif : NULL;
}
bool esp_netif_is_netif_up(esp_netif_t *n) { assert(n == &netif); return up; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *info) {
    assert(n == &netif); info->ip.addr = ipv4; return ESP_OK;
}
int esp_netif_get_all_preferred_ip6(esp_netif_t *n, esp_ip6_addr_t *out) {
    assert(n == &netif); if (!up) return 0;
    memcpy(out, addresses, (size_t)count * sizeof(*out)); return count;
}
esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(const esp_ip6_addr_t *address) { return types[address->addr[3]]; }
struct client { esp_http_client_config_t config; bool cleaned; const char *range; };
static struct client clients[2];
static int initialized, cleaned, opened, failures;
static bool allocation_failure, header_failure;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    if (allocation_failure) return NULL;
    assert(initialized < 2);
    struct client *c = &clients[initialized++]; *c = (struct client){.config = *config}; return c;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) {
    assert(strcmp(key, "Range") == 0); c->range = value; return header_failure ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int length) {
    assert(!c->cleaned && length == 0); return opened++ < failures ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { assert(!c->cleaned); c->cleaned = true; ++cleaned; return ESP_OK; }
static char selected_scheme[8];
static esp_tls_addr_family_t selected_family;
esp_transport_handle_t esp_mqtt_client_get_transport(esp_mqtt_client_handle_t c, char *scheme) {
    assert(c == &netif); snprintf(selected_scheme, sizeof(selected_scheme), "%s", scheme); return c;
}
void esp_transport_ssl_set_addr_family(esp_transport_handle_t c, esp_tls_addr_family_t family) { assert(c == &netif); selected_family = family; }
static void reset_http(void) { initialized = cleaned = opened = failures = 0; allocation_failure = header_failure = false; }
int main(void) {
    char host[48]; esp_ip6_addr_t result;
    assert(!network_station_ready());
    assert(network_station_http_host(host, sizeof(host)) == ESP_ERR_INVALID_STATE);
    count = 1; addresses[0].addr[3] = 0; types[0] = ESP_IP6_ADDR_IS_LINK_LOCAL;
    assert(!network_station_ready());
    count = 2; addresses[1].addr[0] = 0x2001; addresses[1].addr[3] = 1; types[1] = ESP_IP6_ADDR_IS_GLOBAL;
    assert(network_station_ready());
    assert(network_get_ip6_address(&netif, &result) == ESP_OK && result.addr[0] == 0x2001);
    count = 3; addresses[2].addr[0] = 0xfd12; addresses[2].addr[3] = 2; types[2] = ESP_IP6_ADDR_IS_UNIQUE_LOCAL;
    assert(network_get_ip6_address(&netif, &result) == ESP_OK && result.addr[0] == 0xfd12);
    assert(network_station_http_host(host, sizeof(host)) == ESP_OK && host[0] == '[' && strstr(host, "fd12:"));
    assert(network_station_http_host(host, 3) == ESP_ERR_INVALID_SIZE);
    ipv4 = 73;
    assert(network_station_http_host(host, sizeof(host)) == ESP_OK && strcmp(host, "10.0.0.73") == 0);
    up = false; assert(!network_station_ready()); up = true;
    present = false; assert(!network_station_ready()); present = true;

    esp_http_client_config_t config = {.url = "https://release.example/image", .timeout_ms = 30000,
        .crt_bundle_attach = &netif, .cert_pem = "trusted CA"};
    esp_http_client_handle_t client;
    reset_http(); failures = 1;
    assert(network_http_open(&config, "bytes=10-20", &client) == ESP_OK);
    assert(initialized == 2 && opened == 2 && cleaned == 1 && client == &clients[1]);
    assert(clients[0].config.addr_type == HTTP_ADDR_TYPE_INET && clients[1].config.addr_type == HTTP_ADDR_TYPE_INET6);
    for (int i = 0; i < 2; ++i) {
        assert(clients[i].config.url == config.url && clients[i].config.cert_pem == config.cert_pem);
        assert(clients[i].config.crt_bundle_attach == config.crt_bundle_attach);
        assert(!clients[i].config.skip_cert_common_name_check && clients[i].config.timeout_ms == 30000);
        assert(strcmp(clients[i].range, "bytes=10-20") == 0);
    }
    esp_http_client_cleanup(client);
    reset_http(); ipv4 = 0; failures = 1;
    assert(network_http_open(&config, NULL, &client) == ESP_OK);
    assert(clients[0].config.addr_type == HTTP_ADDR_TYPE_INET6 && clients[1].config.addr_type == HTTP_ADDR_TYPE_INET);
    esp_http_client_cleanup(client);
    reset_http(); failures = 2;
    assert(network_http_open(&config, NULL, &client) == ESP_FAIL && client == NULL && cleaned == 2);
    reset_http(); allocation_failure = true;
    assert(network_http_open(&config, NULL, &client) == ESP_ERR_NO_MEM && opened == 0);
    reset_http(); header_failure = true;
    assert(network_http_open(&config, "bytes=10-20", &client) == ESP_FAIL && initialized == 1 && cleaned == 1 && opened == 0);
    reset_http(); config.url = "https://[fd12::1]/image"; ipv4 = 73; failures = 1;
    assert(network_http_open(&config, NULL, &client) == ESP_FAIL && initialized == 1 && cleaned == 1);
    assert(clients[0].config.addr_type == HTTP_ADDR_TYPE_INET6);

    network_mqtt_before_connect(&netif, "mqtts://broker.example", false);
    assert(strcmp(selected_scheme, "mqtts") == 0 && selected_family == ESP_TLS_AF_INET);
    network_mqtt_before_connect(&netif, "mqtts://broker.example", true);
    assert(selected_family == ESP_TLS_AF_INET6);
    ipv4 = 0; network_mqtt_before_connect(&netif, "mqtt://broker.example", false);
    assert(strcmp(selected_scheme, "mqtt") == 0 && selected_family == ESP_TLS_AF_INET6);
    ipv4 = 73; network_mqtt_before_connect(&netif, "mqtts://[fd12::1]:8883", true);
    assert(selected_family == ESP_TLS_AF_INET6);
    return 0;
}
'''
    code = tmp_path / 'network.c'
    code.write_text(harness)
    executable = tmp_path / 'network'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(tmp_path), '-I'+str(ROOT/'include'),
                    str(ROOT/'src/network_client.c'), str(code), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_station_events_and_delayed_mqtt_start(tmp_path):
    source = (ROOT / 'src/wifi_manager.c').read_text()
    handler = source[source.index('static void wifi_event_handler('):source.index('esp_err_t wifi_manager_start(')]
    startup = (ROOT / 'src/app_main.c').read_text()
    worker = startup[startup.index('static void network_start_task('):startup.index('void app_main(')]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_CONNECTED 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_GOT_IP6 4
#define IP_EVENT_STA_LOST_IP 5
#define IP_EVENT_STA_GOT_IP 6
#define STA_GOT_IP_BIT 1U
#define STA_DISCONNECTED_BIT 2U
#define STA_GOT_IP6_BIT 4U
#define STA_READY_BITS (STA_GOT_IP_BIT | STA_GOT_IP6_BIT)
#define ESP_IP6_ADDR_IS_LINK_LOCAL 0
#define ESP_IP6_ADDR_IS_GLOBAL 1
#define ESP_IP6_ADDR_IS_UNIQUE_LOCAL 2
typedef int esp_ip6_addr_type_t;
typedef struct { int type; } esp_ip6_addr_t;
typedef struct { void *esp_netif; struct { esp_ip6_addr_t ip; } ip6_info; } ip_event_got_ip6_t;
typedef struct { void *esp_netif; struct { int ip; } ip_info; } ip_event_got_ip_t;
typedef struct { unsigned reason; } wifi_event_sta_disconnected_t;
#define IPV6STR "%d"
#define IPV62STR(ip) (ip).type
#define IPSTR "%d"
#define IP2STR(ip) *(ip)
static const char *TAG = "test";
static int station, foreign;
static void *s_sta_netif = &station;
static unsigned bits;
static bool s_provisioning_mode;
static void *s_station_events = &bits;
static int connected, linklocals;
static bool ready;
static int delays, mqtt_starts, task_deletes, mqtt_result;
static void log_event(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) log_event(__VA_ARGS__)
#define ESP_LOGE(...) log_event(__VA_ARGS__)
#define ESP_LOGW(...) log_event(__VA_ARGS__)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) ((void)(expr))
static void xEventGroupSetBits(void *group, unsigned value) { assert(group == &bits); bits |= value; }
static void xEventGroupClearBits(void *group, unsigned value) { assert(group == &bits); bits &= ~value; }
static esp_err_t esp_wifi_connect(void) { ++connected; return ESP_OK; }
static esp_err_t esp_netif_create_ip6_linklocal(void *n) { assert(n == &station); ++linklocals; return ESP_OK; }
static esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(const esp_ip6_addr_t *a) { return a->type; }
#define pdMS_TO_TICKS(ms) (ms)
static bool wifi_manager_station_ready(void) { return ready; }
static void vTaskDelay(int ticks) { assert(ticks == 1000); if (++delays == 3) ready = true; }
static void vTaskDelete(void *task) { assert(task == NULL); ++task_deletes; }
static void ui_set_wifi_state(const char *text) { assert(ready); (void)text; }
static void ui_set_connection_status(const char *text) { (void)text; }
static void ui_set_mqtt_state(const char *text) { (void)text; }
static void on_mqtt_connected(void) {}
static void on_mqtt_message(void) {}
static esp_err_t mqtt_app_start(void (*connected_cb)(void), void (*message_cb)(void), void *context) {
    assert(ready && connected_cb == on_mqtt_connected && message_cb == on_mqtt_message && context == NULL);
    ++mqtt_starts; return mqtt_result;
}
static const char *esp_err_to_name(int error) { (void)error; return "error"; }
'''
    harness += handler + worker + r'''
int main(void) {
    wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    assert(linklocals == 1);
    ip_event_got_ip6_t ip6 = {.esp_netif = &station, .ip6_info.ip.type = ESP_IP6_ADDR_IS_LINK_LOCAL};
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_GOT_IP6, &ip6);
    assert(!(bits & STA_READY_BITS));
    ip6.ip6_info.ip.type = ESP_IP6_ADDR_IS_UNIQUE_LOCAL;
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_GOT_IP6, &ip6);
    assert(bits & STA_GOT_IP6_BIT);
    wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(!(bits & STA_READY_BITS) && (bits & STA_DISCONNECTED_BIT) && connected == 1);
    ip6.esp_netif = &foreign;
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_GOT_IP6, &ip6);
    assert(!(bits & STA_READY_BITS));
    ip6.esp_netif = &station; ip6.ip6_info.ip.type = ESP_IP6_ADDR_IS_GLOBAL;
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_GOT_IP6, &ip6);
    assert(bits & STA_GOT_IP6_BIT);
    ip_event_got_ip_t ip4 = {.esp_netif = &station, .ip_info.ip = 73};
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip4);
    assert((bits & STA_READY_BITS) == STA_READY_BITS);
    wifi_event_handler(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
    assert(!(bits & STA_GOT_IP_BIT) && (bits & STA_GOT_IP6_BIT));
    s_provisioning_mode = true;
    wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(connected == 1 && !(bits & STA_READY_BITS));
    network_start_task(NULL);
    assert(delays == 3 && mqtt_starts == 1 && task_deletes == 1);
    mqtt_result = ESP_FAIL;
    network_start_task(NULL);
    assert(delays == 3 && mqtt_starts == 2 && task_deletes == 2);
    return 0;
}
'''
    code = tmp_path / 'events.c'
    code.write_text(harness)
    executable = tmp_path / 'events'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(code), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
