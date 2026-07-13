#include "walldisplay/device_info.h"

#include "walldisplay/app_config.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <stdio.h>
#include <string.h>

#define DEVICE_INFO_INTERVAL_US (60LL * 1000LL * 1000LL)
#define CONTRACT_VERSION_MAX_LEN 16
#define BLUEPRINT_VERSION_MAX_LEN 32

static const char *TAG = "device_info";
static device_info_publish_cb_t s_publish_cb;
static esp_timer_handle_t s_refresh_timer;
static char s_blueprint_version[BLUEPRINT_VERSION_MAX_LEN] = "unknown";
static char s_blueprint_contract_version[CONTRACT_VERSION_MAX_LEN] = "unknown";

static const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

static void format_mac(const uint8_t mac[6], char output[18]) {
    snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t device_info_publish(void) {
    if (s_publish_cb == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    wifi_ap_record_t ap_info = {0};
    uint8_t mac[6] = {0};
    char mac_text[18] = "unknown";
    char bssid_text[18] = "unknown";
    char ip_text[16] = "0.0.0.0";

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        format_mac(mac, mac_text);
    }
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        esp_ip4addr_ntoa(&ip_info.ip, ip_text, sizeof(ip_text));
    }
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        format_mac(ap_info.bssid, bssid_text);
        cJSON_AddStringToObject(root, "ssid", (const char *) ap_info.ssid);
        cJSON_AddNumberToObject(root, "wifi_rssi", ap_info.rssi);
        cJSON_AddNumberToObject(root, "wifi_channel", ap_info.primary);
        cJSON_AddStringToObject(root, "wifi_bssid", bssid_text);
    } else {
        cJSON_AddStringToObject(root, "ssid", app_config_get()->wifi_ssid);
        cJSON_AddNullToObject(root, "wifi_rssi");
        cJSON_AddNullToObject(root, "wifi_channel");
        cJSON_AddStringToObject(root, "wifi_bssid", bssid_text);
    }

    cJSON_AddStringToObject(root, "ip", ip_text);
    cJSON_AddStringToObject(root, "mac", mac_text);
    cJSON_AddStringToObject(root, "firmware_version", APP_FW_VERSION);
    cJSON_AddStringToObject(root, "firmware_contract_version", APP_CONTRACT_VERSION);
    cJSON_AddStringToObject(root, "blueprint_version", s_blueprint_version);
    cJSON_AddStringToObject(root, "blueprint_contract_version", s_blueprint_contract_version);
    cJSON_AddBoolToObject(root, "contract_match",
                          strcmp(s_blueprint_contract_version, APP_CONTRACT_VERSION) == 0);
    cJSON_AddStringToObject(root, "app_partition", partition != NULL ? partition->label : "unknown");
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_name(esp_reset_reason()));
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000LL);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "min_free_psram", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = s_publish_cb("state/device", payload, true);
    cJSON_free(payload);
    return err;
}

bool device_info_set_blueprint_info(const char *version, const char *contract_version) {
    if (version == NULL || version[0] == '\0' || strlen(version) >= sizeof(s_blueprint_version) ||
        contract_version == NULL || contract_version[0] == '\0' ||
        strlen(contract_version) >= sizeof(s_blueprint_contract_version)) {
        strlcpy(s_blueprint_version, "invalid", sizeof(s_blueprint_version));
        strlcpy(s_blueprint_contract_version, "invalid", sizeof(s_blueprint_contract_version));
        return false;
    }
    strlcpy(s_blueprint_version, version, sizeof(s_blueprint_version));
    strlcpy(s_blueprint_contract_version, contract_version, sizeof(s_blueprint_contract_version));
    return strcmp(s_blueprint_contract_version, APP_CONTRACT_VERSION) == 0;
}

static void device_info_timer_cb(void *arg) {
    (void) arg;
    esp_err_t err = device_info_publish();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Device information publish failed: %s", esp_err_to_name(err));
    }
}

esp_err_t device_info_init(device_info_publish_cb_t publish_cb) {
    if (publish_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_publish_cb = publish_cb;
    const esp_timer_create_args_t timer_args = {
        .callback = device_info_timer_cb,
        .name = "device_info",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_refresh_timer), TAG, "timer create failed");
    return esp_timer_start_periodic(s_refresh_timer, DEVICE_INFO_INTERVAL_US);
}
