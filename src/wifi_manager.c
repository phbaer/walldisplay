#include "walldisplay/wifi_manager.h"

#include "walldisplay/app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "wifi_manager";
static bool s_wifi_started;
static esp_netif_t *s_sta_netif;

/* ESP32-S3 accepts quarter-dBm units; 78 is the 19.5 dBm driver maximum.
 * The active regulatory domain and AP may still enforce a lower value. */
#define WIFI_MAX_TX_POWER_QDBM 78

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void) arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi station started, connecting");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
                esp_wifi_connect();
                break;
            default:
                break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Wi-Fi connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_manager_start(void) {
    const app_config_t *config = app_config_get();

    ESP_RETURN_ON_FALSE(config->wifi_ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "Wi-Fi SSID is empty");
    ESP_RETURN_ON_FALSE(strcmp(config->wifi_ssid, "YOUR_WIFI_SSID") != 0, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi SSID is still using placeholder config");

    if (s_wifi_started) {
        ESP_LOGI(TAG, "Wi-Fi already started");
        return ESP_OK;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create Wi-Fi station interface");
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(s_sta_netif, APP_DEVICE_ID), TAG, "Failed to set default hostname");

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi event register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip event register failed");

    wifi_config_t wifi_config = {0};
    /* Copy bounded, NUL-terminated configuration strings without triggering
     * truncation warnings when the compiler uses -Werror. */
    const size_t ssid_len = strnlen(config->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    const size_t password_len = strnlen(config->wifi_password, sizeof(wifi_config.sta.password) - 1);
    memcpy(wifi_config.sta.ssid, config->wifi_ssid, ssid_len);
    memcpy(wifi_config.sta.password, config->wifi_password, password_len);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Starting Wi-Fi station for SSID '%s'", config->wifi_ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
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
    return ESP_OK;
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
