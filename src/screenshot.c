#include "walldisplay/screenshot.h"

#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_spiffs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SCREENSHOT_MOUNT_PATH "/spiffs"
#define SCREENSHOT_FILE_PATH SCREENSHOT_MOUNT_PATH "/screenshot.bmp"
#define SCREENSHOT_TEMP_PATH SCREENSHOT_MOUNT_PATH "/screenshot.tmp"
#define SCREENSHOT_URI "/screenshot.bmp"
#define SCREENSHOT_BYTES_PER_PIXEL 2U
#define SCREENSHOT_BMP_HEADER_SIZE 54U
#define SCREENSHOT_BMP_BYTES_PER_PIXEL 3U

static const char *TAG = "screenshot";

static const display_board_handle_t *s_board;
static screenshot_status_cb_t s_status_callback;
static QueueHandle_t s_capture_queue;
static httpd_handle_t s_http_server;

static void put_u16_le(uint8_t *dest, uint16_t value) {
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *dest, uint32_t value) {
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

static void publish_status(const char *state, size_t bytes) {
    if (s_status_callback == NULL) {
        return;
    }

    char url[80] = "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(url, sizeof(url), "http://" IPSTR SCREENSHOT_URI, IP2STR(&ip_info.ip));
    }

    char payload[180];
    if (url[0] != '\0' && strcmp(state, "ready") == 0) {
        snprintf(payload, sizeof(payload), "{\"state\":\"%s\",\"url\":\"%s\",\"bytes\":%u}",
                 state, url, (unsigned int)bytes);
    } else {
        snprintf(payload, sizeof(payload), "{\"state\":\"%s\"}", state);
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(s_status_callback("state/screenshot", payload, true));
}

static esp_err_t write_bmp(const uint8_t *frame, uint32_t source_stride) {
    const uint32_t row_size = BOARD_LCD_WIDTH * SCREENSHOT_BMP_BYTES_PER_PIXEL;
    const uint32_t file_size = SCREENSHOT_BMP_HEADER_SIZE + row_size * BOARD_LCD_HEIGHT;
    uint8_t header[SCREENSHOT_BMP_HEADER_SIZE] = {0};
    uint8_t row[BOARD_LCD_WIDTH * SCREENSHOT_BMP_BYTES_PER_PIXEL];
    FILE *file = fopen(SCREENSHOT_TEMP_PATH, "wb");

    if (file == NULL) {
        return ESP_FAIL;
    }

    header[0] = 'B';
    header[1] = 'M';
    put_u32_le(&header[2], file_size);
    put_u32_le(&header[10], SCREENSHOT_BMP_HEADER_SIZE);
    put_u32_le(&header[14], 40);
    put_u32_le(&header[18], BOARD_LCD_WIDTH);
    put_u32_le(&header[22], BOARD_LCD_HEIGHT);
    put_u16_le(&header[26], 1);
    put_u16_le(&header[28], 24);
    put_u32_le(&header[34], row_size * BOARD_LCD_HEIGHT);

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        unlink(SCREENSHOT_TEMP_PATH);
        return ESP_FAIL;
    }

    for (int y = BOARD_LCD_HEIGHT - 1; y >= 0; --y) {
        const uint16_t *source = (const uint16_t *)(frame + ((size_t)y * source_stride));
        for (size_t x = 0; x < BOARD_LCD_WIDTH; ++x) {
            /* The display uses RGB565_SWAPPED, so restore CPU-endian RGB565. */
            const uint16_t swapped = source[x];
            const uint16_t rgb565 = (uint16_t)((swapped << 8) | (swapped >> 8));
            row[x * 3] = (uint8_t)((rgb565 & 0x1fU) * 255U / 31U);
            row[x * 3 + 1] = (uint8_t)(((rgb565 >> 5) & 0x3fU) * 255U / 63U);
            row[x * 3 + 2] = (uint8_t)(((rgb565 >> 11) & 0x1fU) * 255U / 31U);
        }
        if (fwrite(row, 1, sizeof(row), file) != sizeof(row)) {
            fclose(file);
            unlink(SCREENSHOT_TEMP_PATH);
            return ESP_FAIL;
        }
    }

    if (fclose(file) != 0 || rename(SCREENSHOT_TEMP_PATH, SCREENSHOT_FILE_PATH) != 0) {
        unlink(SCREENSHOT_TEMP_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t capture_frame(void) {
    const size_t frame_bytes = (size_t)BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * SCREENSHOT_BYTES_PER_PIXEL;
    uint8_t *frame = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_FAIL;
    if (!lvgl_port_lock(1000)) {
        free(frame);
        return ESP_ERR_TIMEOUT;
    }

    lv_draw_buf_t *active = s_board != NULL ? lv_display_get_buf_active(s_board->display) : NULL;
    if (active != NULL && active->data != NULL && active->header.w == BOARD_LCD_WIDTH &&
        active->header.h == BOARD_LCD_HEIGHT && active->header.stride >= BOARD_LCD_WIDTH * SCREENSHOT_BYTES_PER_PIXEL) {
        for (size_t y = 0; y < BOARD_LCD_HEIGHT; ++y) {
            memcpy(frame + y * BOARD_LCD_WIDTH * SCREENSHOT_BYTES_PER_PIXEL,
                   active->data + y * active->header.stride,
                   BOARD_LCD_WIDTH * SCREENSHOT_BYTES_PER_PIXEL);
        }
        result = ESP_OK;
    }
    lvgl_port_unlock();

    if (result == ESP_OK) {
        result = write_bmp(frame, BOARD_LCD_WIDTH * SCREENSHOT_BYTES_PER_PIXEL);
    }
    free(frame);
    return result;
}

static void screenshot_task(void *arg) {
    (void)arg;
    uint8_t request;
    while (true) {
        if (xQueueReceive(s_capture_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        publish_status("capturing", 0);
        const esp_err_t result = capture_frame();
        if (result == ESP_OK) {
            const size_t file_size = SCREENSHOT_BMP_HEADER_SIZE +
                                     (size_t)BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * SCREENSHOT_BMP_BYTES_PER_PIXEL;
            ESP_LOGI(TAG, "Screenshot saved to %s", SCREENSHOT_FILE_PATH);
            publish_status("ready", file_size);
        } else {
            ESP_LOGW(TAG, "Screenshot capture failed: %s", esp_err_to_name(result));
            publish_status("error", 0);
        }
    }
}

static esp_err_t screenshot_http_get(httpd_req_t *request) {
    FILE *file = fopen(SCREENSHOT_FILE_PATH, "rb");
    if (file == NULL) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "No screenshot captured yet");
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "image/bmp");
    httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=walldisplay-screenshot.bmp");
    uint8_t buffer[1024];
    size_t read;
    esp_err_t result = ESP_OK;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        result = httpd_resp_send_chunk(request, (const char *)buffer, read);
        if (result != ESP_OK) {
            break;
        }
    }
    fclose(file);
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, NULL, 0);
    }
    return result;
}

esp_err_t screenshot_init(const display_board_handle_t *board, screenshot_status_cb_t status_callback) {
    if (board == NULL || board->display == NULL || status_callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_capture_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_vfs_spiffs_conf_t spiffs = {
        .base_path = SCREENSHOT_MOUNT_PATH,
        .partition_label = "storage",
        .max_files = 2,
        /* This partition is dedicated to generated screenshots and is blank after flashing. */
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&spiffs), TAG, "SPIFFS mount failed");

    s_board = board;
    s_status_callback = status_callback;
    s_capture_queue = xQueueCreate(1, sizeof(uint8_t));
    ESP_RETURN_ON_FALSE(s_capture_queue != NULL, ESP_ERR_NO_MEM, TAG, "Screenshot queue allocation failed");

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 1;
    server_config.stack_size = 4096;
    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &server_config), TAG, "HTTP server start failed");
    const httpd_uri_t uri = {
        .uri = SCREENSHOT_URI,
        .method = HTTP_GET,
        .handler = screenshot_http_get,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &uri), TAG, "HTTP URI registration failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(screenshot_task, "screenshot", 4096, NULL, 4, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Screenshot task creation failed");
    return ESP_OK;
}

esp_err_t screenshot_request(void) {
    if (s_capture_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t request = 1;
    return xQueueSend(s_capture_queue, &request, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}
