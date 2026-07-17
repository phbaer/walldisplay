#include "walldisplay/media_artwork.h"
#include "walldisplay/ui.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp32s3/rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstring>

namespace {

constexpr size_t kArtworkSize = 112;
constexpr size_t kArtworkUrlMax = 384;
constexpr size_t kMaxDownload = 1024 * 1024;
constexpr size_t kWorkSize = 8192;
constexpr const char *kTag = "media_art";

struct ArtworkRequest {
    char url[kArtworkUrlMax];
};

struct DecodeContext {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint16_t *pixels;
    UINT width;
    UINT height;
};

class ArtworkService {
public:
    esp_err_t init() {
        for (auto &pixels : pixels_) {
            pixels = static_cast<uint16_t *>(heap_caps_malloc(kArtworkSize * kArtworkSize * sizeof(*pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        queue_ = xQueueCreate(1, sizeof(ArtworkRequest));
        if (pixels_[0] == nullptr || pixels_[1] == nullptr || queue_ == nullptr || xTaskCreate(task_entry, "artwork", 6144, this, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
        return ESP_OK;
    }

    esp_err_t request(const char *url) {
        if (url == nullptr || url[0] == '\0') {
            last_url_[0] = '\0';
            return ui_set_media_artwork(nullptr, 0, 0);
        }
        if (std::strcmp(last_url_, url) == 0) return ESP_OK;
        ArtworkRequest request{};
        strlcpy(request.url, url, sizeof(request.url));
        strlcpy(last_url_, request.url, sizeof(last_url_));
        return xQueueOverwrite(queue_, &request) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
    }

private:
    static void task_entry(void *argument) {
        static_cast<ArtworkService *>(argument)->run();
        vTaskDelete(nullptr);
    }

    static UINT jpeg_input(JDEC *decoder, BYTE *buffer, UINT count) {
        auto *context = static_cast<DecodeContext *>(decoder->device);
        if (context == nullptr || context->position >= context->size) return 0;
        size_t available = context->size - context->position;
        size_t read_count = count > available ? available : count;
        if (buffer != nullptr) std::memcpy(buffer, context->data + context->position, read_count);
        context->position += read_count;
        return static_cast<UINT>(read_count);
    }

    static UINT jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect) {
        auto *context = static_cast<DecodeContext *>(decoder->device);
        const auto *rgb = static_cast<const uint8_t *>(bitmap);
        const UINT block_width = rect->right - rect->left + 1;
        for (UINT y = rect->top; y <= rect->bottom; ++y) {
            const UINT target_y_first = y * kArtworkSize / context->height;
            const UINT target_y_end = (y + 1) * kArtworkSize / context->height;
            for (UINT x = rect->left; x <= rect->right; ++x) {
                const UINT target_x_first = x * kArtworkSize / context->width;
                const UINT target_x_end = (x + 1) * kArtworkSize / context->width;
                const size_t pixel = (static_cast<size_t>(y - rect->top) * block_width + x - rect->left) * 3;
                const uint16_t color = static_cast<uint16_t>(
                    ((rgb[pixel] & 0xf8) << 8) | ((rgb[pixel + 1] & 0xfc) << 3) | (rgb[pixel + 2] >> 3));
                for (UINT target_y = target_y_first; target_y < target_y_end && target_y < kArtworkSize; ++target_y) {
                    for (UINT target_x = target_x_first; target_x < target_x_end && target_x < kArtworkSize; ++target_x) {
                        context->pixels[target_y * kArtworkSize + target_x] = color;
                    }
                }
            }
        }
        return 1;
    }

    void run() {
        auto *download = static_cast<uint8_t *>(heap_caps_malloc(kMaxDownload, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        auto *work = static_cast<uint8_t *>(heap_caps_malloc(kWorkSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        ArtworkRequest request{};
        while (xQueueReceive(queue_, &request, portMAX_DELAY) == pdTRUE) {
            const bool is_https = std::strncmp(request.url, "https://", 8) == 0;
            if ((!is_https && std::strncmp(request.url, "http://", 7) != 0) || download == nullptr || work == nullptr) {
                ESP_LOGW(kTag, "Artwork URL must use HTTP(S), and PSRAM must be available");
                continue;
            }

            esp_http_client_config_t config{};
            config.url = request.url;
            config.timeout_ms = 15000;
            if (is_https) config.crt_bundle_attach = esp_crt_bundle_attach;
            esp_http_client_handle_t client = esp_http_client_init(&config);
            const esp_err_t open_result = client == nullptr ? ESP_ERR_NO_MEM : esp_http_client_open(client, 0);
            const int content_length = open_result == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
            const int status_code = client == nullptr ? 0 : esp_http_client_get_status_code(client);
            if (client == nullptr || open_result != ESP_OK || status_code != 200) {
                ESP_LOGW(kTag, "Artwork download failed (open=%s, status=%d, length=%d)", esp_err_to_name(open_result), status_code, content_length);
                if (client != nullptr) esp_http_client_cleanup(client);
                continue;
            }

            size_t total = 0;
            int received = 0;
            while (total < kMaxDownload && (received = esp_http_client_read(client, reinterpret_cast<char *>(download + total), kMaxDownload - total)) > 0) total += static_cast<size_t>(received);
            esp_http_client_cleanup(client);

            DecodeContext context{};
            context.data = download;
            context.size = total;
            const size_t decode_buffer = active_buffer_ ^ 1U;
            context.pixels = pixels_[decode_buffer];
            JDEC decoder{};
            if (jd_prepare(&decoder, jpeg_input, work, kWorkSize, &context) != JDR_OK) {
                ESP_LOGW(kTag, "Artwork is not a baseline JPEG (header=%02x%02x%02x%02x)",
                         total > 0 ? download[0] : 0, total > 1 ? download[1] : 0,
                         total > 2 ? download[2] : 0, total > 3 ? download[3] : 0);
                continue;
            }
            BYTE scale = 0;
            while (scale < 3 && ((decoder.width >> scale) > kArtworkSize * 2 || (decoder.height >> scale) > kArtworkSize * 2)) ++scale;
            context.width = decoder.width >> scale;
            context.height = decoder.height >> scale;
            std::memset(context.pixels, 0, kArtworkSize * kArtworkSize * sizeof(*context.pixels));
            if (jd_decomp(&decoder, jpeg_output, scale) == JDR_OK) {
                ESP_LOGI(kTag, "Artwork rendered (%ux%u, %u bytes)", decoder.width, decoder.height, static_cast<unsigned>(total));
                if (ui_set_media_artwork(context.pixels, kArtworkSize, kArtworkSize) == ESP_OK) active_buffer_ = decode_buffer;
                else ESP_LOGW(kTag, "Artwork display update failed");
            }
            else ESP_LOGW(kTag, "Artwork JPEG decode failed");
        }
    }

    QueueHandle_t queue_ = nullptr;
    uint16_t *pixels_[2]{};
    size_t active_buffer_ = 0;
    char last_url_[kArtworkUrlMax]{};
};

ArtworkService s_artwork_service;

}  // namespace

extern "C" esp_err_t media_artwork_init(void) {
    return s_artwork_service.init();
}

extern "C" esp_err_t media_artwork_request(const char *url) {
    return s_artwork_service.request(url);
}
