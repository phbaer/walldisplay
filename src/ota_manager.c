#include "walldisplay/ota_manager.h"
#include "walldisplay/display_board.h"
#include "walldisplay/media_artwork.h"
#include "walldisplay/mqtt_app.h"
#include "walldisplay/security_policy.h"
#include "walldisplay/ui.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "mbedtls/md.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTA_URL_MAX_LEN 384
#define OTA_MANIFEST_MAX_LEN 4096
#define OTA_DOWNLOAD_BUFFER_LEN 4096
#define OTA_DOWNLOAD_CHUNK_LEN 32768
#define OTA_TASK_STACK_SIZE 8192
#define OTA_TASK_PRIORITY 5

static const char *TAG = "ota_manager";
static QueueHandle_t s_request_queue;
static ota_status_publish_cb_t s_publish_cb;
static char s_manifest_json[OTA_MANIFEST_MAX_LEN];
static uint8_t s_download_buffer[OTA_DOWNLOAD_BUFFER_LEN];
static esp_timer_handle_t s_verify_timer;

static bool signed_updates_enabled(void) {
#if defined(CONFIG_SECURE_SIGNED_ON_UPDATE) && CONFIG_SECURE_SIGNED_ON_UPDATE
    return true;
#else
    return false;
#endif
}

static void verification_timeout(void *arg) {
    (void)arg;
    /* A pending image that cannot connect must actually reboot to roll back. */
    esp_ota_mark_app_invalid_rollback_and_reboot();
}


typedef struct {
    char manifest_url[OTA_URL_MAX_LEN];
} ota_request_t;

typedef struct {
    char firmware_url[OTA_URL_MAX_LEN];
    uint8_t sha256[32];
    size_t size;
    char version[33];
} ota_manifest_t;

static void publish_status(const char *state, const char *detail, const char *version) {
    if (s_publish_cb == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "state", state);
    if (detail != NULL) {
        cJSON_AddStringToObject(root, "detail", detail);
    }
    if (version != NULL) {
        cJSON_AddStringToObject(root, "version", version);
    }
    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(s_publish_cb("state/update", payload, true));
        cJSON_free(payload);
    }
    cJSON_Delete(root);
}

static bool is_https_url(const char *url) {
    return url != NULL && strncmp(url, "https://", strlen("https://")) == 0;
}

static bool decode_sha256(const char *hex, uint8_t output[32]) {
    if (hex == NULL || strlen(hex) != 64) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        const unsigned char high = (unsigned char) hex[i * 2];
        const unsigned char low = (unsigned char) hex[i * 2 + 1];
        if (!isxdigit(high) || !isxdigit(low)) {
            return false;
        }
        const unsigned int high_value = isdigit(high) ? high - '0' : (tolower(high) - 'a' + 10);
        const unsigned int low_value = isdigit(low) ? low - '0' : (tolower(low) - 'a' + 10);
        output[i] = (uint8_t) ((high_value << 4) | low_value);
    }
    return true;
}

static esp_err_t read_manifest(const char *url, char *buffer, size_t buffer_len) {
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* Release hosts may publish an unusable link-local AAAA record.  OTA
         * URLs are HTTPS and retain hostname verification, while connecting
         * through IPv4 avoids selecting that unroutable address. */
        .addr_type = HTTP_ADDR_TYPE_INET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        const int content_length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200 || content_length < 0 || (size_t) content_length >= buffer_len) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    size_t total = 0;
    while (err == ESP_OK && total < buffer_len - 1) {
        const int received = esp_http_client_read(client, buffer + total, buffer_len - 1 - total);
        if (received < 0) {
            err = ESP_FAIL;
            break;
        }
        if (received == 0) {
            break;
        }
        total += (size_t) received;
    }
    if (err == ESP_OK && total == buffer_len - 1) {
        err = ESP_ERR_INVALID_SIZE;
    }
    buffer[total] = '\0';
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t parse_manifest(const char *json, ota_manifest_t *manifest) {
    if (!json_depth_safe(json)) return ESP_ERR_INVALID_RESPONSE;
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *sha256 = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(root, "target");
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;

    if (cJSON_IsString(url) && cJSON_IsString(sha256) && cJSON_IsNumber(size) &&
        cJSON_IsString(version) && cJSON_IsString(target) && strcmp(target->valuestring, "esp32s3") == 0 &&
        is_https_url(url->valuestring) && size->valuedouble > 0 && size->valuedouble <= UINT32_MAX &&
        size->valuedouble == (double) (size_t) size->valuedouble && strlen(url->valuestring) < sizeof(manifest->firmware_url) &&
        strlen(version->valuestring) < sizeof(manifest->version) &&
        decode_sha256(sha256->valuestring, manifest->sha256)) {
        strlcpy(manifest->firmware_url, url->valuestring, sizeof(manifest->firmware_url));
        strlcpy(manifest->version, version->valuestring, sizeof(manifest->version));
        manifest->size = (size_t) size->valuedouble;
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t download_firmware(const ota_manifest_t *manifest) {
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->size > partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(partition, manifest->size, &ota_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *buffer = s_download_buffer;
    uint8_t calculated_sha256[32];
    size_t total = 0;
    mbedtls_md_context_t sha_context;
    mbedtls_md_init(&sha_context);
    const mbedtls_md_info_t *sha256_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256_info == NULL || mbedtls_md_setup(&sha_context, sha256_info, 0) != 0 ||
        mbedtls_md_starts(&sha_context) != 0) {
        err = ESP_FAIL;
    }
    while (err == ESP_OK && total < manifest->size) {
        const size_t chunk_size = (manifest->size - total) < OTA_DOWNLOAD_CHUNK_LEN
                                      ? manifest->size - total : OTA_DOWNLOAD_CHUNK_LEN;
        esp_http_client_config_t config = {
            .url = manifest->firmware_url,
            .timeout_ms = 30000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            /* Release hosts may publish an unusable link-local AAAA record. */
            .addr_type = HTTP_ADDR_TYPE_INET,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        char range[64];
        snprintf(range, sizeof(range), "bytes=%u-%u", (unsigned) total,
                 (unsigned) (total + chunk_size - 1));
        err = esp_http_client_set_header(client, "Range", range);
        if (err == ESP_OK) err = esp_http_client_open(client, 0);
        const int content_length = err == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
        if (err == ESP_OK && (esp_http_client_get_status_code(client) != 206 ||
                              content_length != (int) chunk_size)) {
            ESP_LOGE(TAG, "Firmware range %s returned status=%d length=%d", range,
                     esp_http_client_get_status_code(client), content_length);
            err = ESP_ERR_INVALID_RESPONSE;
        }
        size_t chunk_total = 0;
        while (err == ESP_OK && chunk_total < chunk_size) {
            const size_t remaining = chunk_size - chunk_total;
            const int received = esp_http_client_read(client, (char *) buffer,
                                                      remaining < OTA_DOWNLOAD_BUFFER_LEN ? remaining : OTA_DOWNLOAD_BUFFER_LEN);
            if (received <= 0) {
                ESP_LOGE(TAG, "Firmware HTTP range %s stopped at %u/%u bytes (result=%d, complete=%d)",
                         range, (unsigned) chunk_total, (unsigned) chunk_size, received,
                         esp_http_client_is_complete_data_received(client));
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (mbedtls_md_update(&sha_context, buffer, (size_t) received) != 0 ||
                esp_ota_write(ota_handle, buffer, (size_t) received) != ESP_OK) {
                err = ESP_FAIL;
                break;
            }
            chunk_total += (size_t) received;
            total += (size_t) received;
        }
        if (err == ESP_OK && (!esp_http_client_is_complete_data_received(client) || chunk_total != chunk_size)) {
            err = ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_cleanup(client);
        if (err == ESP_OK) {
            const uint8_t percent = (uint8_t) ((total * 100ULL) / manifest->size);
            ESP_LOGI(TAG, "Firmware download progress: %u%% (%u/%u bytes)",
                     (unsigned) percent, (unsigned) total, (unsigned) manifest->size);
            ESP_ERROR_CHECK_WITHOUT_ABORT(ui_set_update_progress(percent));
        }
    }
    if (err == ESP_OK && mbedtls_md_finish(&sha_context, calculated_sha256) != 0) {
        err = ESP_FAIL;
    }
    mbedtls_md_free(&sha_context);

    if (err == ESP_OK && (total != manifest->size || memcmp(calculated_sha256, manifest->sha256, sizeof(calculated_sha256)) != 0)) {
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) {
        err = esp_ota_end(ota_handle);
    } else {
        esp_ota_abort(ota_handle);
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(partition);
    }
    return err;
}

static void ota_task(void *arg) {
    (void) arg;
    ota_request_t request;

    ota_manifest_t manifest;

    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        publish_status("checking", "Downloading manifest", NULL);
        /* The running MQTT TLS session and artwork fetch can otherwise leave
         * too little contiguous internal RAM for the 16 KiB HTTPS record. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(media_artwork_cancel());
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_app_stop());
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_err_t err = read_manifest(request.manifest_url, s_manifest_json, sizeof(s_manifest_json));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Manifest download failed: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_app_restart());
            publish_status("error", "Unable to download update manifest", NULL);
            ESP_ERROR_CHECK_WITHOUT_ABORT(ui_hide_update_screen());
            continue;
        }
        err = parse_manifest(s_manifest_json, &manifest);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Manifest validation failed: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_app_restart());
            publish_status("error", "Update manifest is invalid", NULL);
            ESP_ERROR_CHECK_WITHOUT_ABORT(ui_hide_update_screen());
            continue;
        }

        publish_status("installing", "Downloading firmware", manifest.version);
        /* Flash writes temporarily contend with the RGB DMA path for cache and
         * PSRAM bandwidth. Lowering PCLK keeps the static update screen visible
         * while reducing that contention; the board falls back to a dark
         * transfer if its RGB driver cannot change PCLK. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(display_board_enter_ota());
        err = download_firmware(&manifest);
        ESP_LOGI(TAG, "OTA stack minimum free: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Firmware update failed: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(display_board_exit_ota());
            ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_app_restart());
            publish_status("error", "Firmware download or verification failed", manifest.version);
            ESP_ERROR_CHECK_WITHOUT_ABORT(ui_hide_update_screen());
            continue;
        }
        publish_status("rebooting", "Firmware verified", manifest.version);
        ESP_ERROR_CHECK_WITHOUT_ABORT(display_board_prepare_for_restart());
        /* Let the backlight shutdown and blank frame reach the panel before
         * the reset sequence starts. */
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
}

esp_err_t ota_manager_init(ota_status_publish_cb_t publish_cb) {
    if (publish_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_publish_cb = publish_cb;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_timer_create_args_t timer = {.callback = verification_timeout, .name = "ota_verify"};
        esp_err_t err = esp_timer_create(&timer, &s_verify_timer);
        if (err != ESP_OK) return err;
        err = esp_timer_start_once(s_verify_timer, 120ULL * 1000000ULL);
        if (err != ESP_OK) return err;
    }
    s_request_queue = xQueueCreate(1, sizeof(ota_request_t));
    if (s_request_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ota_task, "ota_update", OTA_TASK_STACK_SIZE, NULL, OTA_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_manager_request(const char *manifest_url) {
    if (!signed_updates_enabled()) {
        publish_status("error", "OTA requires a signed-app verification build", NULL);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_request_queue == NULL || !is_https_url(manifest_url) || strlen(manifest_url) >= OTA_URL_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_request_t request = {0};
    strlcpy(request.manifest_url, manifest_url, sizeof(request.manifest_url));
    if (xQueueSend(s_request_queue, &request, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(ui_show_update_screen());
    publish_status("queued", "Update request accepted", NULL);
    return ESP_OK;
}

esp_err_t ota_manager_mark_running_image_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking verified OTA image valid");
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK && s_verify_timer != NULL) esp_timer_stop(s_verify_timer);
        return err;
    }
    return err == ESP_ERR_NOT_SUPPORTED ? ESP_OK : err;
}
