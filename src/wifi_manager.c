#include "walldisplay/wifi_manager.h"

#include "walldisplay/app_config.h"
#include "walldisplay/network_client.h"
#include "walldisplay/setup_portal.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_manager";
static bool s_wifi_started;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_ap_active;
static bool s_provisioning_mode;
static SemaphoreHandle_t s_ap_mutex;
static TaskHandle_t s_ap_timeout_task;
static EventGroupHandle_t s_station_events;
#define STA_GOT_IP_BIT BIT0
#define STA_DISCONNECTED_BIT BIT1
#define STA_GOT_IP6_BIT BIT2
#define STA_READY_BITS (STA_GOT_IP_BIT | STA_GOT_IP6_BIT)
static int64_t s_ap_last_activity_us;
static wifi_config_t s_saved_station_config;
static bool s_saved_station_config_valid;

#define AP_IDLE_TIMEOUT_MS (15U * 60U * 1000U)
/* Three groups of four symbols; see ap_password_generate(). */
#define AP_PASSWORD_LEN 14
static char s_ap_password[AP_PASSWORD_LEN + 1];

static void ap_timeout_task(void *arg) {
    (void)arg;
    TickType_t wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, wait_ticks) != 0) {
            wait_ticks = pdMS_TO_TICKS(AP_IDLE_TIMEOUT_MS);
            continue;
        }
        /* An unprovisioned panel has no other way to be configured, so its
         * setup AP stays up. The timeout applies to an AP enabled on demand. */
        if (s_ap_active && !s_provisioning_mode) {
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

void wifi_manager_ap_activity(void) {
    s_ap_last_activity_us = esp_timer_get_time();
    if (s_ap_timeout_task != NULL) {
        xTaskNotifyGive(s_ap_timeout_task);
    }
}

/* The setup AP password is the only access control of the setup portal, so it
 * must not be derivable from anything the panel broadcasts (its SSID and BSSID
 * expose the MAC address). It is drawn from the hardware RNG once per boot and
 * shown only on the panel display and the serial console. Twelve symbols from
 * a 31-character alphabet without look-alikes give about 59 bits. */
static void ap_password_generate(void) {
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    const unsigned symbols = sizeof(alphabet) - 1;
    if (s_ap_password[0] != '\0') return;
    size_t out = 0;
    while (out < AP_PASSWORD_LEN) {
        if (out == 4 || out == 9) { s_ap_password[out++] = '-'; continue; }
        uint8_t value;
        esp_fill_random(&value, sizeof(value));
        /* Reject the tail of the byte range so every symbol is equally likely. */
        if (value >= 256U - 256U % symbols) continue;
        s_ap_password[out++] = alphabet[value % symbols];
    }
    s_ap_password[out] = '\0';
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
            case WIFI_EVENT_STA_CONNECTED:
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_create_ip6_linklocal(s_sta_netif));
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_station_events) xEventGroupClearBits(s_station_events, STA_READY_BITS);
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

    if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        const ip_event_got_ip6_t *event = event_data;
        if (event->esp_netif != s_sta_netif) return;
        ESP_LOGI(TAG, "Wi-Fi IPv6: " IPV6STR, IPV62STR(event->ip6_info.ip));
        const esp_ip6_addr_type_t type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
        if (s_station_events && (type == ESP_IP6_ADDR_IS_GLOBAL || type == ESP_IP6_ADDR_IS_UNIQUE_LOCAL)) {
            xEventGroupSetBits(s_station_events, STA_GOT_IP6_BIT);
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        if (s_station_events) xEventGroupClearBits(s_station_events, STA_GOT_IP_BIT);
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
        if (event->esp_netif != s_sta_netif) return;
        ESP_LOGI(TAG, "Wi-Fi connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
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
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "ip event register failed");

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
    /* Do not let app_main race ahead and permanently skip MQTT startup while
     * the station is still associating. A bounded wait preserves boot
     * recovery when the configured network is unavailable. */
    if (s_station_events != NULL) {
        (void)xEventGroupWaitBits(s_station_events, STA_READY_BITS,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    }
    return ESP_OK;
}

/* Verify a candidate without touching NVS. The old driver configuration is
 * restored on failure, so a typo in the portal cannot strand the panel. */
esp_err_t wifi_manager_test_station(const char *ssid, const char *password) {
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
    xEventGroupClearBits(s_station_events, STA_READY_BITS | STA_DISCONNECTED_BIT);
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &candidate);
    if (err == ESP_OK) err = esp_wifi_connect();
    EventBits_t bits = 0;
    if (err == ESP_OK) bits = xEventGroupWaitBits(s_station_events, STA_READY_BITS,
                                                   pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
    if (err != ESP_OK || !(bits & STA_READY_BITS) || !network_station_ready()) {
        ESP_LOGW(TAG, "Candidate Wi-Fi connection failed; restoring previous network");
        (void) esp_wifi_disconnect();
        (void) esp_wifi_set_config(WIFI_IF_STA, &previous);
        xEventGroupClearBits(s_station_events, STA_READY_BITS | STA_DISCONNECTED_BIT);
        (void) esp_wifi_connect();
        s_saved_station_config_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "Candidate Wi-Fi connection verified");
    return ESP_OK;
}

void wifi_manager_restore_station(void) {
    if (!s_saved_station_config_valid) return;
    (void) esp_wifi_disconnect();
    (void) esp_wifi_set_config(WIFI_IF_STA, &s_saved_station_config);
    xEventGroupClearBits(s_station_events, STA_READY_BITS | STA_DISCONNECTED_BIT);
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
    network_family_t family;
    unsigned transport_failures;
    const char *uri;
} mqtt_probe_t;
static void mqtt_probe_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)base;
    mqtt_probe_t *probe = arg;
    if (id == MQTT_EVENT_BEFORE_CONNECT) {
        network_mqtt_before_connect(((esp_mqtt_event_handle_t)data)->client, probe->uri, &probe->family);
    } else if (id == MQTT_EVENT_CONNECTED) {
        probe->connected = true;
        xEventGroupSetBits(probe->events, BIT0);
    } else if (id == MQTT_EVENT_ERROR) {
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
        if (event && event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT &&
            ++probe->transport_failures < 2 && network_family_has_alternative(&probe->family)) {
            network_family_result(&probe->family, false);
        } else {
            probe->failed = true;
            xEventGroupSetBits(probe->events, BIT1);
        }
    }
}

esp_err_t wifi_manager_test_mqtt(const char *uri, const char *username, const char *password,
                                        bool require_tls, const char *ca_certificate) {
    if (!uri || uri[0] == '\0') return ESP_OK;
    const bool tls = strncmp(uri, "mqtts://", 8) == 0;
    if (!tls && (require_tls || strncmp(uri, "mqtt://", 7) != 0)) {
        ESP_LOGW(TAG, "MQTT probe rejected: URI/security mismatch (URI must be mqtts://, or mqtt:// with TLS disabled)");
        return ESP_ERR_INVALID_ARG;
    }
    mqtt_probe_t probe = {.events = xEventGroupCreate(), .uri = uri};
    if (!probe.events) {
        ESP_LOGW(TAG, "MQTT probe could not allocate its event group");
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_config_t config = {
        .broker.address.uri = uri,
        .network.timeout_ms = 8000,
        .network.reconnect_timeout_ms = 100,
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
                                                     pdMS_TO_TICKS(30000));
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

esp_err_t wifi_manager_start_ap(void) {
    if (s_ap_mutex == NULL) {
        s_ap_mutex = xSemaphoreCreateMutex();
        if (s_ap_mutex == NULL) return ESP_ERR_NO_MEM;
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
    ap_password_generate();
    strlcpy((char *) config.ap.password, s_ap_password, sizeof(config.ap.password));
    config.ap.ssid_len = strlen((char *) config.ap.ssid);
    config.ap.channel = 1; config.ap.max_connection = 2; config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    err = setup_portal_start(s_ap_netif);
    if (err != ESP_OK) { xSemaphoreGive(s_ap_mutex); return err; }
    if (s_ap_timeout_task == NULL) {
        if (xTaskCreate(ap_timeout_task, "wd_ap_timeout", 3072, NULL, 3, &s_ap_timeout_task) != pdPASS) {
            ESP_LOGW(TAG, "Could not create setup AP timeout task");
        }
    }
    s_ap_active = true;
    s_ap_last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Setup AP enabled (SSID %s, password %s)", config.ap.ssid, config.ap.password);
    xSemaphoreGive(s_ap_mutex);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    if (s_ap_mutex != NULL) xSemaphoreTake(s_ap_mutex, portMAX_DELAY);
    if (s_ap_timeout_task != NULL && s_ap_timeout_task != xTaskGetCurrentTaskHandle()) {
        xTaskNotifyGive(s_ap_timeout_task);
    }
    setup_portal_stop();
    s_ap_active = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (s_ap_mutex != NULL) xSemaphoreGive(s_ap_mutex);
    return err;
}

bool wifi_manager_ap_active(void) { return s_ap_active; }
bool wifi_manager_station_ready(void) { return network_station_ready(); }

esp_err_t wifi_manager_get_ap_credentials(char *ssid, size_t ssid_size,
                                          char *password, size_t password_size) {
    if (!ssid || !password || ssid_size < 14 || password_size <= AP_PASSWORD_LEN) return ESP_ERR_INVALID_ARG;
    if (s_ap_password[0] == '\0') return ESP_ERR_INVALID_STATE;
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    int n = snprintf(ssid, ssid_size, "WallDisplay-%02X%02X", mac[4], mac[5]);
    if (n < 0 || (size_t)n >= ssid_size) return ESP_ERR_INVALID_SIZE;
    strlcpy(password, s_ap_password, password_size);
    return ESP_OK;
}
