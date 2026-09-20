#include "walldisplay/wifi_manager.h"

#include "walldisplay/app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "wifi_manager";
static bool s_wifi_started;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_portal;
static bool s_ap_active;
static bool s_station_ready;
static bool s_provisioning_mode;
static TaskHandle_t s_dns_task;
static int s_dns_socket = -1;
static int s_portal_attempts;
static int64_t s_portal_window_us;
static SemaphoreHandle_t s_ap_mutex;
static SemaphoreHandle_t s_scan_mutex;
static TaskHandle_t s_ap_timeout_task;
static EventGroupHandle_t s_station_events;
#define STA_GOT_IP_BIT BIT0
#define STA_DISCONNECTED_BIT BIT1
static int64_t s_ap_last_activity_us;
static wifi_config_t s_saved_station_config;
static bool s_saved_station_config_valid;

#define AP_IDLE_TIMEOUT_MS (15U * 60U * 1000U)

static void ap_timeout_task(void *arg) {
    (void)arg;
    TickType_t wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, wait_ticks) != 0) {
            wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
            continue;
        }
        if (s_ap_active) {
            const int64_t idle_us = esp_timer_get_time() - s_ap_last_activity_us;
            if (idle_us >= (int64_t)AP_IDLE_TIMEOUT_MS * 1000LL) {
                ESP_LOGI(TAG, "Setup AP idle timeout reached; stopping AP");
                (void)wifi_manager_stop_ap();
                wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
            } else {
                const int64_t remaining_us = (int64_t)AP_IDLE_TIMEOUT_MS * 1000LL - idle_us;
                wait_ticks = pdMS_TO_TICKS((uint32_t)((remaining_us + 999LL) / 1000LL));
            }
        } else {
            wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
        }
    }
}

static void portal_activity(void) {
    s_ap_last_activity_us = esp_timer_get_time();
    if (s_ap_timeout_task != NULL) {
        xTaskNotifyGive(s_ap_timeout_task);
    }
}

static void dns_task(void *arg) {
    (void)arg;
    uint8_t packet[256];
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    while (s_dns_socket >= 0) {
        int length = recvfrom(s_dns_socket, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &peer_len);
        if (length < 12 || length + 16 > (int)sizeof(packet)) continue;
        const int question_end = length;
        packet[length++] = 0xc0; packet[length++] = 0x0c;
        packet[length++] = 0x00; packet[length++] = 0x01;
        packet[length++] = 0x00; packet[length++] = 0x01;
        packet[length++] = 0x00; packet[length++] = 0x00; packet[length++] = 0x00; packet[length++] = 0x3c;
        packet[length++] = 0x00; packet[length++] = 0x04;
        packet[length++] = 192; packet[length++] = 168; packet[length++] = 4; packet[length++] = 1;
        packet[2] = 0x81; packet[3] = 0x80; packet[6] = 0; packet[7] = 1;
        sendto(s_dns_socket, packet, length, 0, (struct sockaddr *)&peer, peer_len);
        (void)question_end;
    }
    vTaskDelete(NULL);
}

static void dns_start(void) {
    if (s_dns_task != NULL) return;
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) return;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s_dns_socket, (struct sockaddr *)&address, sizeof(address)) != 0) { close(s_dns_socket); s_dns_socket = -1; return; }
    xTaskCreate(dns_task, "wd_dns", 3072, NULL, 4, &s_dns_task);
}

static void html_escape(const char *input, char *output, size_t size) {
    size_t out = 0;
    for (size_t i = 0; input != NULL && input[i] != '\0' && out + 6 < size; ++i) {
        const char *replacement = NULL;
        switch (input[i]) { case '&': replacement = "&amp;"; break; case '<': replacement = "&lt;"; break; case '>': replacement = "&gt;"; break; case '"': replacement = "&quot;"; break; case '\'': replacement = "&#39;"; break; default: break; }
        if (replacement != NULL) { size_t n = strlen(replacement); memcpy(output + out, replacement, n); out += n; }
        else output[out++] = input[i];
    }
    output[out] = '\0';
}

static void json_escape(const char *input, char *output, size_t size) {
    size_t out = 0;
    for (size_t i = 0; input != NULL && input[i] != '\0' && out + 2 < size; ++i) {
        unsigned char c = (unsigned char)input[i];
        if (c == '\\' || c == '"') { output[out++] = '\\'; output[out++] = (char)c; }
        else if (c >= 0x20 && c != 0x7f) output[out++] = (char)c;
    }
    output[out] = '\0';
}

static int hex_digit(unsigned char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void form_url_decode(char *value) {
    char *read = value, *write = value;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (read[0] == '%' && read[1] != '\0' && read[2] != '\0') {
            const int high = hex_digit((unsigned char)read[1]);
            const int low = hex_digit((unsigned char)read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = (char)((high << 4) | low);
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

/* ESP32-S3 accepts quarter-dBm units; 78 is the 19.5 dBm driver maximum.
 * The active regulatory domain and AP may still enforce a lower value. */
#define WIFI_MAX_TX_POWER_QDBM 78

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void) arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi station started%s", s_provisioning_mode ? " (provisioning; connection held)" : ", connecting");
                if (!s_provisioning_mode) esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (event_data != NULL) {
                    const wifi_event_sta_disconnected_t *disconnected = event_data;
                    ESP_LOGW(TAG, "Wi-Fi disconnected, reason %u, retrying", disconnected->reason);
                } else {
                    ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
                }
                if (s_station_events) xEventGroupSetBits(s_station_events, STA_DISCONNECTED_BIT);
                if (!s_provisioning_mode) esp_wifi_connect();
                break;
            default:
                break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Wi-Fi connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_station_ready = true;
        if (s_station_events) xEventGroupSetBits(s_station_events, STA_GOT_IP_BIT);
    }
}

esp_err_t wifi_manager_start(void) {
    const app_config_t *config = app_config_get();
    const bool provisioning = config->wifi_ssid[0] == '\0' || strcmp(config->wifi_ssid, "YOUR_WIFI_SSID") == 0;
    s_provisioning_mode = provisioning;

    if (s_wifi_started) {
        ESP_LOGI(TAG, "Wi-Fi already started");
        return ESP_OK;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create Wi-Fi station interface");
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(s_sta_netif, APP_DEVICE_ID), TAG, "Failed to set default hostname");

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init failed");
    s_station_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_station_events != NULL, ESP_ERR_NO_MEM, TAG, "station event group allocation failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi event register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip event register failed");

    wifi_config_t wifi_config = {0};
    /* Copy bounded, NUL-terminated configuration strings without triggering
     * truncation warnings when the compiler uses -Werror. */
    const size_t ssid_len = strnlen(config->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    const size_t password_len = strnlen(config->wifi_password, sizeof(wifi_config.sta.password) - 1);
    memcpy(wifi_config.sta.ssid, config->wifi_ssid, ssid_len);
    memcpy(wifi_config.sta.password, config->wifi_password, password_len);
    /* Match the authentication threshold to the provisioned network. Open
     * networks are valid portal targets and must not be filtered out after a
     * reboot simply because the default threshold is WPA2. */
    wifi_config.sta.threshold.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "%s", provisioning ? "Starting Wi-Fi provisioning AP" : "Starting Wi-Fi station");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(provisioning ? WIFI_MODE_APSTA : WIFI_MODE_STA), TAG, "set mode failed");
    /* Always overwrite the driver's STA profile. Otherwise an erased or
     * unconfigured application can reconnect using credentials left in the
     * Wi-Fi driver's own NVS namespace while the setup AP is running. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set config failed");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    const esp_err_t tx_power_err = esp_wifi_set_max_tx_power(WIFI_MAX_TX_POWER_QDBM);
    if (tx_power_err != ESP_OK) {
        ESP_LOGW(TAG, "Could not set maximum Wi-Fi transmit power: %s", esp_err_to_name(tx_power_err));
    } else {
        int8_t tx_power_qdbm = 0;
        if (esp_wifi_get_max_tx_power(&tx_power_qdbm) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi maximum transmit power: %.2f dBm", tx_power_qdbm / 4.0f);
        }
    }

    s_wifi_started = true;
    if (provisioning) return wifi_manager_start_ap();
    s_station_ready = false;
    /* Do not let app_main race ahead and permanently skip MQTT startup while
     * the station is still associating. A bounded wait preserves boot
     * recovery when the configured network is unavailable. */
    if (s_station_events != NULL) {
        EventBits_t bits = xEventGroupWaitBits(s_station_events, STA_GOT_IP_BIT,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
        s_station_ready = (bits & STA_GOT_IP_BIT) != 0;
    }
    return ESP_OK;
}

/* Verify a candidate without touching NVS. The old driver configuration is
 * restored on failure, so a typo in the portal cannot strand the panel. */
static esp_err_t wifi_manager_test_station(const char *ssid, const char *password) {
    if (!s_wifi_started || !ssid || !password || !s_station_events) return ESP_ERR_INVALID_STATE;
    wifi_config_t previous = {0}, candidate = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &previous) != ESP_OK) return ESP_FAIL;
    s_saved_station_config = previous;
    s_saved_station_config_valid = true;
    size_t ssid_len = strnlen(ssid, sizeof(candidate.sta.ssid) - 1);
    size_t pass_len = strnlen(password, sizeof(candidate.sta.password) - 1);
    memcpy(candidate.sta.ssid, ssid, ssid_len);
    memcpy(candidate.sta.password, password, pass_len);
    /* Match the normal station setup.  Leaving this at OPEN makes the Wi-Fi
     * driver rewrite it to WPA2 when the candidate password is long enough,
     * which produces a misleading threshold warning and can reject the probe
     * before association with some access points. */
    candidate.sta.threshold.authmode = pass_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    candidate.sta.pmf_cfg.capable = true;
    candidate.sta.pmf_cfg.required = false;
    xEventGroupClearBits(s_station_events, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
    s_station_ready = false;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &candidate);
    if (err == ESP_OK) err = esp_wifi_connect();
    EventBits_t bits = 0;
    if (err == ESP_OK) bits = xEventGroupWaitBits(s_station_events, STA_GOT_IP_BIT,
                                                   pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
    if (err != ESP_OK || !(bits & STA_GOT_IP_BIT)) {
        ESP_LOGW(TAG, "Candidate Wi-Fi connection failed; restoring previous network");
        (void) esp_wifi_disconnect();
        (void) esp_wifi_set_config(WIFI_IF_STA, &previous);
        xEventGroupClearBits(s_station_events, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
        (void) esp_wifi_connect();
        s_station_ready = false;
        s_saved_station_config_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "Candidate Wi-Fi connection verified");
    return ESP_OK;
}

static void wifi_manager_restore_saved_station(void) {
    if (!s_saved_station_config_valid) return;
    (void) esp_wifi_disconnect();
    (void) esp_wifi_set_config(WIFI_IF_STA, &s_saved_station_config);
    xEventGroupClearBits(s_station_events, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
    s_station_ready = false;
    if (s_saved_station_config.sta.ssid[0] != '\0' &&
        strcmp((const char *)s_saved_station_config.sta.ssid, "YOUR_WIFI_SSID") != 0) {
        (void) esp_wifi_connect();
    }
    s_saved_station_config_valid = false;
}

typedef struct {
    EventGroupHandle_t events;
    bool connected;
    bool failed;
} mqtt_probe_t;
static void mqtt_probe_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)base;
    mqtt_probe_t *probe = arg;
    if (id == MQTT_EVENT_CONNECTED) {
        probe->connected = true;
        xEventGroupSetBits(probe->events, BIT0);
    } else if (id == MQTT_EVENT_ERROR) {
        probe->failed = true;
        esp_mqtt_event_handle_t event = data;
        if (event != NULL && event->error_handle != NULL) {
            const esp_mqtt_error_codes_t *error = event->error_handle;
            ESP_LOGW(TAG, "MQTT probe failed (type %d, broker code %d, TLS %s/%d, socket %d)",
                     error->error_type, error->connect_return_code,
                     esp_err_to_name(error->esp_tls_last_esp_err),
                     error->esp_tls_stack_err, error->esp_transport_sock_errno);
        } else {
            ESP_LOGW(TAG, "MQTT probe failed without error details");
        }
        xEventGroupSetBits(probe->events, BIT1);
    }
}

static esp_err_t wifi_manager_test_mqtt(const char *uri, const char *username, const char *password,
                                        bool require_tls, const char *ca_certificate) {
    if (!uri || uri[0] == '\0') return ESP_OK;
    const bool tls = strncmp(uri, "mqtts://", 8) == 0;
    if (!tls && (require_tls || strncmp(uri, "mqtt://", 7) != 0)) {
        ESP_LOGW(TAG, "MQTT probe rejected: URI/security mismatch (URI must be mqtts://, or mqtt:// with TLS disabled)");
        return ESP_ERR_INVALID_ARG;
    }
    mqtt_probe_t probe = {.events = xEventGroupCreate()};
    if (!probe.events) {
        ESP_LOGW(TAG, "MQTT probe could not allocate its event group");
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_config_t config = {
        .broker.address.uri = uri,
        .broker.verification.certificate = tls && ca_certificate && ca_certificate[0] ? ca_certificate : NULL,
        .broker.verification.crt_bundle_attach = tls && (!ca_certificate || !ca_certificate[0]) ? esp_crt_bundle_attach : NULL,
        .credentials.username = username,
        .credentials.authentication.password = password,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
    esp_err_t err = client ? ESP_OK : ESP_ERR_NO_MEM;
    if (client == NULL) {
        ESP_LOGW(TAG, "MQTT probe client initialization failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_probe_handler, &probe);
        if (err != ESP_OK) ESP_LOGW(TAG, "MQTT probe event registration failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(client);
        if (err != ESP_OK) ESP_LOGW(TAG, "MQTT probe start failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        const EventBits_t bits = xEventGroupWaitBits(probe.events, BIT0 | BIT1, pdTRUE, pdFALSE,
                                                     pdMS_TO_TICKS(10000));
        if ((bits & BIT0) == 0) {
            err = (bits & BIT1) ? ESP_FAIL : ESP_ERR_TIMEOUT;
            ESP_LOGW(TAG, "MQTT probe did not connect: %s", esp_err_to_name(err));
        }
    }
    if (client) { (void)esp_mqtt_client_stop(client); (void)esp_mqtt_client_destroy(client); }
    vEventGroupDelete(probe.events);
    return err;
}

esp_err_t wifi_manager_set_hostname(const char *hostname) {
    if (s_sta_netif == NULL || hostname == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen(hostname, 33);
    if (length == 0 || length > 32 || hostname[0] == '-' || hostname[length - 1] == '-') {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char character = (unsigned char) hostname[i];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '-')) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return esp_netif_set_hostname(s_sta_netif, hostname);
}

esp_err_t wifi_manager_set_display_name(const char *display_name) {
    char hostname[33];
    esp_err_t err = app_config_display_hostname(display_name, hostname, sizeof(hostname));
    if (err != ESP_OK) return err;
    err = app_config_set_display_name(display_name);
    if (err != ESP_OK) return err;
    return wifi_manager_set_hostname(hostname);
}

static esp_err_t portal_get(httpd_req_t *req) {
    portal_activity();
    const app_config_t *config = app_config_get();
    char hostname[33] = "walldisplay";
    (void) app_config_display_hostname(config->display_name, hostname, sizeof(hostname));
    char name[APP_DISPLAY_NAME_MAX_LEN * 2 + 1], ssid[APP_WIFI_MAX_SSID_LEN * 2 + 1];
    char mqtt_uri[APP_MQTT_URI_MAX_LEN * 2 + 1], mqtt_user[APP_MQTT_USERNAME_MAX_LEN * 2 + 1];
    char disc_pref[APP_TOPIC_MAX_LEN * 2 + 1], base_topic[APP_TOPIC_MAX_LEN * 2 + 1];
    char topic_prefix[APP_TOPIC_MAX_LEN + 1];
    char generated_topic[APP_TOPIC_MAX_LEN + 1], generated_topic_html[APP_TOPIC_MAX_LEN * 2 + 1];
    char *ca_certificate = malloc(APP_MQTT_CA_MAX_LEN * 2 + 1);
    if (ca_certificate == NULL) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    html_escape(config->display_name, name, sizeof(name)); html_escape(config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(config->mqtt_uri, mqtt_uri, sizeof(mqtt_uri)); html_escape(config->mqtt_username, mqtt_user, sizeof(mqtt_user));
    strlcpy(topic_prefix, config->base_topic, sizeof(topic_prefix));
    char *suffix = strrchr(topic_prefix, '/');
    if (suffix != NULL && strcmp(suffix + 1, hostname) == 0) *suffix = '\0';
    if (app_config_derive_base_topic(topic_prefix, config->display_name, generated_topic, sizeof(generated_topic)) != ESP_OK)
        strlcpy(generated_topic, config->base_topic, sizeof(generated_topic));
    html_escape(generated_topic, generated_topic_html, sizeof(generated_topic_html));
    html_escape(config->discovery_prefix, disc_pref, sizeof(disc_pref)); html_escape(topic_prefix, base_topic, sizeof(base_topic));
    html_escape(config->mqtt_ca_certificate, ca_certificate, sizeof(ca_certificate));
    char *body = malloc(16384);
    if (body == NULL) {
        free(ca_certificate);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    snprintf(body, 16384,
        "<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>WallDisplay setup</title>"
        "<style>*,*:before,*:after{box-sizing:border-box}html,body{overflow-x:hidden}body{font:16px system-ui,sans-serif;background:#f4f6f8;color:#18212b;margin:0;padding:clamp(10px,4vw,24px)}main{width:100%%;max-width:620px;min-width:0;margin:auto;background:white;padding:clamp(16px,4vw,28px);border-radius:14px;box-shadow:0 4px 18px #0001}h1{margin-top:0;font-size:clamp(1.35rem,5vw,1.8rem)}h2{font-size:1.05rem;border-bottom:1px solid #dde2e7;padding-bottom:8px;margin-top:24px}label{display:block;margin:14px 0 5px;font-weight:600}input,textarea{box-sizing:border-box;width:100%%;min-width:0;padding:10px;border:1px solid #b7c1ca;border-radius:7px;font:inherit}textarea{resize:vertical;max-width:100%%}button{padding:10px 15px;border:0;border-radius:7px;background:#1769aa;color:white;font:inherit;cursor:pointer}button.secondary{background:#e4e9ee;color:#18212b}.row{display:flex;gap:8px;align-items:end;flex-wrap:wrap;min-width:0}.row input{min-width:0;flex:1 1 200px}.row button{white-space:nowrap}.hint{color:#5f6c78;font-size:.9rem}.network{display:flex;justify-content:space-between;gap:8px;padding:9px;border-bottom:1px solid #edf0f2;cursor:pointer;overflow-wrap:anywhere}.network:hover{background:#f0f6fb}.status{display:grid;grid-template-columns:max-content minmax(0,1fr);gap:6px 12px;padding:12px;border-radius:7px;background:#eef5fa;margin:12px 0;overflow-wrap:anywhere}.status b{font-weight:700}.status code{overflow-wrap:anywhere}.error,.success{min-height:1.3em}.error{color:#b42318}.success{color:#167447}@media(max-width:420px){main{border-radius:10px}.row button{flex:1 1 auto}}</style></head><body><main>"
        "<h1>WallDisplay setup</h1><div class=status><b>Display name</b><span>%s</span><b>Hostname</b><code id=hostname>%s</code><b>MQTT topic</b><code id=mqtt_topic>%s</code></div>"
        "<form method=post action=/api/config><h2>Wi-Fi</h2>"
        "<label for=ssid>Network name</label><div class=row><input id=ssid name=ssid maxlength=32 value=\"%s\" required autocapitalize=none autocorrect=off spellcheck=false><button type=button class=secondary onclick=scan()>Refresh networks</button></div><div id=networks class=hint>Choose Refresh networks to scan nearby access points.</div>"
        "<label for=password>Wi-Fi password</label><div class=row><input id=password name=password type=password maxlength=64><button type=button class=secondary onclick=toggle('password',this)>Show</button></div>"
        "<h2>Display</h2><label for=name>Display name</label><input id=name name=name maxlength=64 value=\"%s\" required><p class=hint>Spaces and capitalization are preserved. The generated hostname is shown above.</p>"
        "<h2>MQTT (optional)</h2><label for=mqtt_uri>Broker URI</label><input id=mqtt_uri name=mqtt_uri maxlength=128 value=\"%s\" placeholder= mqtts://broker.example autocapitalize=none autocorrect=off spellcheck=false><label>Username</label><input name=mqtt_user maxlength=64 value=\"%s\" autocapitalize=none autocorrect=off spellcheck=false><label>Password</label><div class=row><input id=mqtt_password name=mqtt_password type=password maxlength=64><button type=button class=secondary onclick=toggle('mqtt_password',this)>Show</button></div>"
        "<label>Discovery prefix</label><input name=disc_pref maxlength=128 value=\"%s\"><label>MQTT topic prefix</label><input id=base_topic name=base_topic maxlength=128 value=\"%s\" autocapitalize=none autocorrect=off spellcheck=false><p class=hint>The panel topic is generated as <code>prefix/hostname</code> from this prefix and the display name.</p><label><input style=width:auto name=mqtt_tls type=checkbox value=ON %s> Require MQTT TLS</label><label for=ca_certificate>Custom MQTT CA certificate (optional)</label><textarea id=ca_certificate name=ca_certificate maxlength=3072 rows=7 placeholder=Leave blank to use the built-in CA bundle>%s</textarea><p class=hint>Paste a PEM certificate, including BEGIN/END CERTIFICATE lines. Leave blank to use the built-in CA bundle.</p><label>Screenshot token (optional)</label><div class=row><input id=screenshot_token name=screenshot_token minlength=32 maxlength=64 pattern=\"[A-Za-z0-9]+\" type=password placeholder=Leave unchanged to keep current token><button type=button class=secondary onclick=generateToken()>Generate</button><button type=button class=secondary onclick=toggle('screenshot_token',this)>Show</button></div><p class=hint>Use 32 to 64 letters and numbers. Leave blank to keep the existing token.</p>"
        "<p id=error class=error role=status aria-live=polite></p><div class=row><button type=submit name=action value=test class=secondary>Test connections</button><button type=submit name=action value=save>Save and reboot</button></div></form><form method=post action=/api/reset onsubmit=\"return confirm('Erase saved settings and restart setup?')\"><button class=secondary type=submit>Erase settings and restart setup</button></form><p class=hint>Test connections checks Wi-Fi and MQTT without saving and reports the result here. Saving validates again and restarts the panel.</p></main>"
        "<script>function toggle(id,b){let e=document.getElementById(id);e.type=e.type==='password'?'text':'password';b.textContent=e.type==='password'?'Show':'Hide'}function hostname(v){return v.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-+|-+$/g,'').slice(0,32)}function updateTopic(){let h=hostname(document.getElementById('name').value),p=document.getElementById('base_topic').value.replace(/\\/+$/,'');document.getElementById('hostname').textContent=h||'(invalid)';document.getElementById('mqtt_topic').textContent=h&&p?p+'/'+h:'(invalid)'}function generateToken(){let a=new Uint8Array(48);crypto.getRandomValues(a);let c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789',t='';a.forEach(x=>t+=c[x%%c.length]);let e=document.getElementById('screenshot_token');e.value=t;e.type='text'}async function scan(){let n=document.getElementById('networks');n.textContent='Scanning...';try{let r=await fetch('/api/wifi/scan',{cache:'no-store'});if(!r.ok)throw Error();let a=await r.json();n.innerHTML=a.length?'':'No networks found.';a.forEach(x=>{let d=document.createElement('div');d.className='network';d.innerHTML='<span></span><span></span>';d.firstChild.textContent=x.ssid||'(hidden)';d.lastChild.textContent=x.rssi+' dBm - '+x.security;d.onclick=()=>{document.getElementById('ssid').value=x.ssid};n.appendChild(d)})}catch(e){n.textContent='Scan failed. Enter the network name manually.'}}let form=document.querySelector('form');form.addEventListener('submit',async e=>{let status=document.getElementById('error');if(e.submitter&&e.submitter.value==='test'){e.preventDefault();status.className='error';status.textContent='Testing connections...';let params=new URLSearchParams(new FormData(form));params.set('action','test');try{let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params});status.textContent=await r.text();status.className=r.ok?'success':'error'}catch(err){status.textContent='Connection test request failed';status.className='error'}}else{status.className='error';status.textContent='Saving...'}});document.getElementById('name').addEventListener('input',updateTopic);document.getElementById('base_topic').addEventListener('input',updateTopic);updateTopic()</script></body></html>",
        name, hostname, generated_topic_html, ssid, name, mqtt_uri, mqtt_user, disc_pref, base_topic, config->mqtt_require_tls ? "checked" : "", ca_certificate);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    free(ca_certificate);
    return result;
}

static esp_err_t portal_scan(httpd_req_t *req) {
    portal_activity();
    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan busy");
    }
    wifi_scan_config_t scan_config = {.scan_type = WIFI_SCAN_TYPE_ACTIVE, .show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan unavailable");
    }
    /* The HTTP server task has a small stack. Keep the potentially large AP
     * record array on the heap to avoid corrupting the task stack when a scan
     * is requested from the setup page. */
    wifi_ap_record_t *records = calloc(32, sizeof(*records));
    if (records == NULL) {
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan out of memory");
    }
    uint16_t count = 32;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed");
    }
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    char item[192], escaped[APP_WIFI_MAX_SSID_LEN * 2 + 1];
    bool emitted = false;
    for (uint16_t i = 0; i < count; ++i) {
        if (records[i].ssid[0] == '\0') continue;
        bool duplicate = false;
        for (uint16_t j = 0; j < i; ++j) if (strcmp((char *)records[i].ssid, (char *)records[j].ssid) == 0) { duplicate = true; break; }
        if (duplicate) continue;
        json_escape((char *)records[i].ssid, escaped, sizeof(escaped));
        const char *security = records[i].authmode == WIFI_AUTH_OPEN ? "Open" : "Secured";
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\"}", emitted ? "," : "", escaped, records[i].rssi, security);
        httpd_resp_sendstr_chunk(req, item);
        emitted = true;
    }
    httpd_resp_sendstr_chunk(req, "]");
    err = httpd_resp_sendstr_chunk(req, NULL);
    free(records);
    xSemaphoreGive(s_scan_mutex);
    return err;
}

static esp_err_t portal_status(httpd_req_t *req) {
    portal_activity();
    const app_config_t *config = app_config_get();
    char hostname[33] = "walldisplay";
    (void) app_config_display_hostname(config->display_name, hostname, sizeof(hostname));
    char body[192];
    snprintf(body, sizeof(body), "{\"hostname\":\"%s\",\"ap_active\":%s}",
             hostname, s_ap_active ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t portal_redirect(httpd_req_t *req) {
    portal_activity();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t portal_post(httpd_req_t *req) {
    portal_activity();
    const int64_t now = esp_timer_get_time();
    if (now - s_portal_window_us > 60LL * 1000LL * 1000LL) { s_portal_window_us = now; s_portal_attempts = 0; }
    if (++s_portal_attempts > 5) { httpd_resp_set_status(req, "429 Too Many Requests"); return httpd_resp_send(req, "Try again later", HTTPD_RESP_USE_STRLEN); }
    char content_type[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strncasecmp(content_type, "application/x-www-form-urlencoded", 33) != 0) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_send(req, "Form encoding required", HTTPD_RESP_USE_STRLEN);
    }
    if (req->content_len <= 0 || req->content_len >= 12288) return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request too large");
    char *body = calloc(1, (size_t)req->content_len + 1);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) { free(body); return ESP_FAIL; }
        received += n;
    }
    body[received] = '\0';
    char ssid[APP_WIFI_MAX_SSID_LEN + 1] = {0}, password[APP_WIFI_MAX_PASSWORD_LEN + 1] = {0}, name[APP_DISPLAY_NAME_MAX_LEN + 1] = {0};
    char mqtt_uri[APP_MQTT_URI_MAX_LEN + 1] = {0}, mqtt_user[APP_MQTT_USERNAME_MAX_LEN + 1] = {0}, mqtt_password[APP_MQTT_PASSWORD_MAX_LEN + 1] = {0};
    char disc_pref[APP_TOPIC_MAX_LEN + 1] = {0}, base_topic[APP_TOPIC_MAX_LEN + 1] = {0}, mqtt_tls[8] = {0};
    char screenshot_token[APP_SCREENSHOT_TOKEN_MAX_LEN + 1] = {0};
    char ca_certificate[APP_MQTT_CA_MAX_LEN + 1] = {0};
    char action[16] = {0};
    char hostname[33];
    const app_config_t *current = app_config_get();
    bool has_ca = httpd_query_key_value(body, "ca_certificate", ca_certificate, sizeof(ca_certificate)) == ESP_OK;
    if (has_ca) form_url_decode(ca_certificate);
    if (!has_ca) strlcpy(ca_certificate, current->mqtt_ca_certificate, sizeof(ca_certificate));
    const bool has_password = httpd_query_key_value(body, "password", password, sizeof(password)) == ESP_OK;
    if (has_password) form_url_decode(password);
    const bool has_mqtt_password = httpd_query_key_value(body, "mqtt_password", mqtt_password, sizeof(mqtt_password)) == ESP_OK;
    if (has_mqtt_password) form_url_decode(mqtt_password);
    const bool has_ssid = httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) == ESP_OK;
    if (has_ssid) form_url_decode(ssid);
    const bool has_name = httpd_query_key_value(body, "name", name, sizeof(name)) == ESP_OK;
    if (has_name) form_url_decode(name);
    if (!has_ssid || !has_name) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
    }
    if (!has_password && strcmp(ssid, current->wifi_ssid) == 0) strlcpy(password, current->wifi_password, sizeof(password));
    const bool has_mqtt_uri = httpd_query_key_value(body, "mqtt_uri", mqtt_uri, sizeof(mqtt_uri)) == ESP_OK;
    if (has_mqtt_uri) form_url_decode(mqtt_uri); else strlcpy(mqtt_uri, current->mqtt_uri, sizeof(mqtt_uri));
    const bool has_mqtt_user = httpd_query_key_value(body, "mqtt_user", mqtt_user, sizeof(mqtt_user)) == ESP_OK;
    if (has_mqtt_user) form_url_decode(mqtt_user); else strlcpy(mqtt_user, current->mqtt_username, sizeof(mqtt_user));
    if (!has_mqtt_password && strcmp(mqtt_uri, current->mqtt_uri) == 0) strlcpy(mqtt_password, current->mqtt_password, sizeof(mqtt_password));
    ESP_LOGW(TAG, "Portal MQTT URI received: '%s'", mqtt_uri[0] != '\0' ? mqtt_uri : "(empty)");
    const bool has_disc_pref = httpd_query_key_value(body, "disc_pref", disc_pref, sizeof(disc_pref)) == ESP_OK;
    if (has_disc_pref) form_url_decode(disc_pref); else strlcpy(disc_pref, current->discovery_prefix, sizeof(disc_pref));
    const bool has_base_topic = httpd_query_key_value(body, "base_topic", base_topic, sizeof(base_topic)) == ESP_OK;
    if (has_base_topic) form_url_decode(base_topic); else strlcpy(base_topic, current->base_topic, sizeof(base_topic));
    const bool has_screenshot_token = httpd_query_key_value(body, "screenshot_token", screenshot_token, sizeof(screenshot_token)) == ESP_OK;
    if (has_screenshot_token) form_url_decode(screenshot_token);
    if (!has_screenshot_token || screenshot_token[0] == '\0')
        strlcpy(screenshot_token, current->screenshot_token, sizeof(screenshot_token));
    const bool require_tls = httpd_query_key_value(body, "mqtt_tls", mqtt_tls, sizeof(mqtt_tls)) == ESP_OK;
    const bool test_only = httpd_query_key_value(body, "action", action, sizeof(action)) == ESP_OK &&
                           strcmp(action, "test") == 0;
    ESP_LOGI(TAG, "Portal request action: %s", test_only ? "test" : "save");
    char derived_topic[APP_TOPIC_MAX_LEN + 1];
    if (app_config_derive_base_topic(base_topic, name, derived_topic, sizeof(derived_topic)) != ESP_OK) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MQTT topic prefix");
    }
    const esp_err_t wifi_test_err = wifi_manager_test_station(ssid, password);
    const bool wifi_ok = wifi_test_err == ESP_OK;
    const esp_err_t mqtt_test_err = wifi_ok ? wifi_manager_test_mqtt(mqtt_uri, mqtt_user, mqtt_password,
                                                                      require_tls, ca_certificate) : ESP_ERR_INVALID_STATE;
    const bool mqtt_ok = mqtt_test_err == ESP_OK;
    const esp_err_t hostname_err = app_config_display_hostname(name, hostname, sizeof(hostname));
    const esp_err_t config_err = test_only ? ESP_OK :
        ((wifi_ok && mqtt_ok && hostname_err == ESP_OK)
            ? app_config_set_portal_config(ssid, password, name, mqtt_uri, mqtt_user, mqtt_password,
                                           require_tls, disc_pref, derived_topic, screenshot_token,
                                           ca_certificate)
            : ESP_ERR_INVALID_STATE);
    ESP_LOGI(TAG, "Portal save validation: Wi-Fi %s (%s), MQTT %s (%s), hostname %s, config %s",
             wifi_ok ? "ok" : "failed", esp_err_to_name(wifi_test_err),
             mqtt_ok ? "ok" : "failed", esp_err_to_name(mqtt_test_err),
             hostname_err == ESP_OK ? "ok" : "failed", esp_err_to_name(config_err));
    if (!wifi_ok || !mqtt_ok || hostname_err != ESP_OK || config_err != ESP_OK) {
        if (wifi_ok && (test_only || !mqtt_ok || config_err != ESP_OK)) wifi_manager_restore_saved_station();
        free(body);
        const char *message = !wifi_ok ? "Wi-Fi connection failed; previous configuration was kept" :
                              !mqtt_ok ? "MQTT connection failed; previous configuration was kept" :
                              "Could not save configuration; previous configuration was kept";
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
    }
    if (test_only) {
        wifi_manager_restore_saved_station();
        free(body);
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Wi-Fi and MQTT connections succeeded; nothing was saved.", HTTPD_RESP_USE_STRLEN);
    }
    free(body);
    (void) wifi_manager_set_hostname(hostname);
    httpd_resp_set_type(req, "text/plain");
    esp_err_t result = httpd_resp_send(req, "Saved. Rebooting to connect.", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return result;
}

static esp_err_t portal_reset(httpd_req_t *req) {
    portal_activity();
    if (req->content_len != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unexpected request body");
    const esp_err_t reset_err = app_config_reset_runtime();
    if (reset_err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "Could not erase settings (%s)", esp_err_to_name(reset_err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, message);
    }
    httpd_resp_set_type(req, "text/plain");
    esp_err_t result = httpd_resp_send(req, "Settings erased. Rebooting to setup mode.", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return result;
}

esp_err_t wifi_manager_start_ap(void) {
    if (s_ap_mutex == NULL) {
        s_ap_mutex = xSemaphoreCreateMutex();
        if (s_ap_mutex == NULL) return ESP_ERR_NO_MEM;
    }
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
        if (s_scan_mutex == NULL) return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_ap_mutex, portMAX_DELAY);
    if (s_ap_active) {
        xSemaphoreGive(s_ap_mutex);
        return ESP_OK;
    }
    if (s_ap_netif == NULL) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) { xSemaphoreGive(s_ap_mutex); return ESP_ERR_NO_MEM; }
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    wifi_config_t config = {0};
    snprintf((char *) config.ap.ssid, sizeof(config.ap.ssid), "WallDisplay-%02X%02X", mac[4], mac[5]);
    snprintf((char *) config.ap.password, sizeof(config.ap.password), "wd-%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    config.ap.ssid_len = strlen((char *) config.ap.ssid);
    config.ap.channel = 1; config.ap.max_connection = 2; config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    if (s_portal == NULL) {
        httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
        /* The screenshot server uses the HTTPD default control socket. */
        server_config.ctrl_port = 32769;
        /* portal_post keeps bounded form fields on its stack while parsing;
         * allow room for the optional PEM certificate and HTTPD internals. */
        server_config.stack_size = 8192;
        server_config.max_uri_handlers = 12;
        err = httpd_start(&s_portal, &server_config);
        if (err != ESP_OK) { s_portal = NULL; xSemaphoreGive(s_ap_mutex); return err; }
        const httpd_uri_t get_uri = {.uri = "/", .method = HTTP_GET, .handler = portal_get};
        const httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = portal_status};
        const httpd_uri_t scan_uri = {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = portal_scan};
        const httpd_uri_t post_uri = {.uri = "/api/config", .method = HTTP_POST, .handler = portal_post};
        const httpd_uri_t reset_uri = {.uri = "/api/reset", .method = HTTP_POST, .handler = portal_reset};
        const httpd_uri_t redirect_204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = portal_redirect};
        const httpd_uri_t redirect_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = portal_redirect};
        const httpd_uri_t redirect_connect = {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = portal_redirect};
        const httpd_uri_t redirect_ncsi = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = portal_redirect};
        const esp_err_t get_err = httpd_register_uri_handler(s_portal, &get_uri);
        const esp_err_t status_err = httpd_register_uri_handler(s_portal, &status_uri);
        const esp_err_t scan_err = httpd_register_uri_handler(s_portal, &scan_uri);
        const esp_err_t post_err = httpd_register_uri_handler(s_portal, &post_uri);
        if (get_err != ESP_OK || status_err != ESP_OK || scan_err != ESP_OK || post_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register setup portal API handlers");
        }
        httpd_register_uri_handler(s_portal, &redirect_204); httpd_register_uri_handler(s_portal, &redirect_hotspot);
        httpd_register_uri_handler(s_portal, &redirect_connect); httpd_register_uri_handler(s_portal, &redirect_ncsi);
        if (httpd_register_uri_handler(s_portal, &reset_uri) != ESP_OK)
            ESP_LOGE(TAG, "Failed to register reset endpoint");
        dns_start();
    }
    if (s_ap_timeout_task == NULL) {
        if (xTaskCreate(ap_timeout_task, "wd_ap_timeout", 3072, NULL, 3, &s_ap_timeout_task) != pdPASS) {
            ESP_LOGW(TAG, "Could not create setup AP timeout task");
        }
    }
    s_ap_active = true;
    s_portal_attempts = 0;
    s_portal_window_us = esp_timer_get_time();
    s_ap_last_activity_us = s_portal_window_us;
    ESP_LOGI(TAG, "Setup AP enabled (SSID %s, password %s)", config.ap.ssid, config.ap.password);
    xSemaphoreGive(s_ap_mutex);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    if (s_ap_mutex != NULL) xSemaphoreTake(s_ap_mutex, portMAX_DELAY);
    if (s_ap_timeout_task != NULL && s_ap_timeout_task != xTaskGetCurrentTaskHandle()) {
        xTaskNotifyGive(s_ap_timeout_task);
    }
    if (s_portal != NULL) { httpd_stop(s_portal); s_portal = NULL; }
    if (s_dns_socket >= 0) { close(s_dns_socket); s_dns_socket = -1; s_dns_task = NULL; }
    s_ap_active = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (s_ap_mutex != NULL) xSemaphoreGive(s_ap_mutex);
    return err;
}

bool wifi_manager_ap_active(void) { return s_ap_active; }
bool wifi_manager_station_ready(void) { return s_station_ready; }

esp_err_t wifi_manager_get_ap_credentials(char *ssid, size_t ssid_size,
                                          char *password, size_t password_size) {
    if (!ssid || !password || ssid_size < 14 || password_size < 16) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    int n = snprintf(ssid, ssid_size, "WallDisplay-%02X%02X", mac[4], mac[5]);
    if (n < 0 || (size_t)n >= ssid_size) return ESP_ERR_INVALID_SIZE;
    n = snprintf(password, password_size, "wd-%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    return n < 0 || (size_t)n >= password_size ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
