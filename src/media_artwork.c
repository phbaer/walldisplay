#include "walldisplay/media_artwork.h"
#include "walldisplay/ui.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp32s3/rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

#define ART_SIZE 112
#define ART_URL_MAX 384
#define ART_MAX_DOWNLOAD (256 * 1024)
#define ART_WORK_SIZE 8192

static const char *TAG = "media_art";
static QueueHandle_t s_queue;
static uint16_t *s_pixels;
static char s_last_url[ART_URL_MAX];

typedef struct { char url[ART_URL_MAX]; } artwork_request_t;
typedef struct { const uint8_t *data; size_t size, pos; uint16_t *pixels; UINT width, height; } decode_ctx_t;

static UINT jpeg_input(JDEC *jd, BYTE *buffer, UINT count) {
    decode_ctx_t *ctx = jd->device;
    size_t n = count;
    if (n > ctx->size - ctx->pos) n = ctx->size - ctx->pos;
    if (buffer != NULL) memcpy(buffer, ctx->data + ctx->pos, n);
    ctx->pos += n;
    return (UINT)n;
}

static UINT jpeg_output(JDEC *jd, void *bitmap, JRECT *rect) {
    decode_ctx_t *ctx = jd->device;
    const uint8_t *rgb = bitmap;
    const UINT block_w = rect->right - rect->left + 1;
    for (UINT y = rect->top; y <= rect->bottom; ++y) for (UINT x = rect->left; x <= rect->right; ++x) {
        UINT dx = x * ART_SIZE / ctx->width, dy = y * ART_SIZE / ctx->height;
        if (dx < ART_SIZE && dy < ART_SIZE) {
            size_t p = ((size_t)(y - rect->top) * block_w + x - rect->left) * 3;
            ctx->pixels[dy * ART_SIZE + dx] = (uint16_t)((rgb[p] & 0xf8) << 8 | (rgb[p + 1] & 0xfc) << 3 | rgb[p + 2] >> 3);
        }
    }
    return 1;
}

static void artwork_task(void *arg) {
    (void)arg;
    artwork_request_t request;
    uint8_t *download = heap_caps_malloc(ART_MAX_DOWNLOAD, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *work = heap_caps_malloc(ART_WORK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    while (xQueueReceive(s_queue, &request, portMAX_DELAY) == pdTRUE) {
        if (strncmp(request.url, "https://", 8) != 0 || download == NULL || work == NULL) continue;
        esp_http_client_config_t cfg = {.url = request.url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach};
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client == NULL || esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0 || esp_http_client_get_status_code(client) != 200) { if (client) esp_http_client_cleanup(client); continue; }
        size_t total = 0; int got;
        while (total < ART_MAX_DOWNLOAD && (got = esp_http_client_read(client, (char *)download + total, ART_MAX_DOWNLOAD - total)) > 0) total += got;
        esp_http_client_cleanup(client);
        decode_ctx_t ctx = {.data = download, .size = total, .pixels = s_pixels}; JDEC dec;
        if (jd_prepare(&dec, jpeg_input, &ctx, ART_WORK_SIZE, work) != JDR_OK) { ESP_LOGW(TAG, "Artwork is not a baseline JPEG"); continue; }
        BYTE scale = 0; while (scale < 3 && ((dec.width >> scale) > ART_SIZE * 2 || (dec.height >> scale) > ART_SIZE * 2)) ++scale;
        ctx.width = dec.width >> scale; ctx.height = dec.height >> scale;
        memset(s_pixels, 0, ART_SIZE * ART_SIZE * sizeof(*s_pixels));
        if (jd_decomp(&dec, jpeg_output, scale) == JDR_OK) ui_set_media_artwork(s_pixels, ART_SIZE, ART_SIZE);
    }
    vTaskDelete(NULL);
}

esp_err_t media_artwork_init(void) {
    s_pixels = heap_caps_malloc(ART_SIZE * ART_SIZE * sizeof(*s_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_queue = xQueueCreate(2, sizeof(artwork_request_t));
    return (s_pixels && s_queue && xTaskCreate(artwork_task, "artwork", 6144, NULL, 4, NULL) == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t media_artwork_request(const char *url) {
    if (url == NULL || url[0] == '\0') return ui_set_media_artwork(NULL, 0, 0);
    if (strncmp(url, s_last_url, sizeof(s_last_url)) == 0) return ESP_OK;
    artwork_request_t request = {0}; strlcpy(request.url, url, sizeof(request.url)); strlcpy(s_last_url, url, sizeof(s_last_url));
    return xQueueSend(s_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
