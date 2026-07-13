#include "walldisplay/ota_manager.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/md.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTA_URL_MAX_LEN 384
#define OTA_MANIFEST_MAX_LEN 4096
#define OTA_DOWNLOAD_BUFFER_LEN 4096
#define OTA_TASK_STACK_SIZE 8192
#define OTA_TASK_PRIORITY 5

static const char *TAG = "ota_manager";
static QueueHandle_t s_request_queue;
static ota_status_publish_cb_t s_publish_cb;

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
    esp_http_client_config_t config = {
        .url = manifest->firmware_url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    const int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 ||
        (content_length >= 0 && (size_t) content_length != manifest->size)) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->size > partition->size) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(partition, manifest->size, &ota_handle);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    uint8_t buffer[OTA_DOWNLOAD_BUFFER_LEN];
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
        const size_t remaining = manifest->size - total;
        const int received = esp_http_client_read(client, (char *) buffer,
                                                  remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (received <= 0) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (mbedtls_md_update(&sha_context, buffer, (size_t) received) != 0 ||
            esp_ota_write(ota_handle, buffer, (size_t) received) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        total += (size_t) received;
    }
    if (err == ESP_OK && mbedtls_md_finish(&sha_context, calculated_sha256) != 0) {
        err = ESP_FAIL;
    }
    mbedtls_md_free(&sha_context);
    esp_http_client_cleanup(client);

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
    char manifest_json[OTA_MANIFEST_MAX_LEN];
    ota_manifest_t manifest;

    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        publish_status("checking", "Downloading manifest", NULL);
        esp_err_t err = read_manifest(request.manifest_url, manifest_json, sizeof(manifest_json));
        if (err == ESP_OK) {
            err = parse_manifest(manifest_json, &manifest);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Manifest failed: %s", esp_err_to_name(err));
            publish_status("error", "Manifest validation failed", NULL);
            continue;
        }

        publish_status("installing", "Downloading firmware", manifest.version);
        err = download_firmware(&manifest);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Firmware update failed: %s", esp_err_to_name(err));
            publish_status("error", "Firmware download or verification failed", manifest.version);
            continue;
        }
        publish_status("rebooting", "Firmware verified", manifest.version);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

esp_err_t ota_manager_init(ota_status_publish_cb_t publish_cb) {
    if (publish_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_publish_cb = publish_cb;
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
    if (s_request_queue == NULL || !is_https_url(manifest_url) || strlen(manifest_url) >= OTA_URL_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_request_t request = {0};
    strlcpy(request.manifest_url, manifest_url, sizeof(request.manifest_url));
    if (xQueueSend(s_request_queue, &request, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_status("queued", "Update request accepted", NULL);
    return ESP_OK;
}

esp_err_t ota_manager_mark_running_image_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking verified OTA image valid");
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    return err == ESP_ERR_NOT_SUPPORTED ? ESP_OK : err;
}
