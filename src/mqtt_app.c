#include "walldisplay/mqtt_app.h"

#include "walldisplay/app_config.h"
#include "walldisplay/mqtt_receive.h"
#include "esp_crt_bundle.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt_app";
static esp_mqtt_client_handle_t s_client;
static mqtt_connected_cb_t s_connected_cb;
static mqtt_message_cb_t s_message_cb;
static void *s_connected_ctx;

#define MQTT_RX_TOPIC_MAX_LEN 256
#define MQTT_RX_PAYLOAD_MAX_LEN 2048
#define MQTT_TX_TOPIC_MAX_LEN 192
#define MQTT_TX_PAYLOAD_MAX_LEN 64
#define MQTT_TX_QUEUE_DEPTH 12

typedef struct { char topic[MQTT_TX_TOPIC_MAX_LEN]; char payload[MQTT_TX_PAYLOAD_MAX_LEN]; bool retain; } mqtt_tx_request_t;

static mqtt_receive_t s_rx;
static char s_availability_topic[APP_TOPIC_MAX_LEN + 16];
static QueueHandle_t s_tx_queue;

static void mqtt_tx_task(void *arg) {
    (void)arg;
    mqtt_tx_request_t request;
    while (true) {
        if (xQueueReceive(s_tx_queue, &request, portMAX_DELAY) == pdTRUE &&
            mqtt_app_publish(request.topic, request.payload, request.retain) != ESP_OK) {
            ESP_LOGW(TAG, "Dropping queued MQTT publish to %s", request.topic);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            if (s_connected_cb != NULL) {
                s_connected_cb(event->client, s_connected_ctx);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_rx.active = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_DATA: {
            if (event->topic_len < 0 || event->data_len < 0 || event->current_data_offset < 0 || event->total_data_len < 0) {
                s_rx.active = false;
                break;
            }
            if (mqtt_receive_feed(&s_rx, event->topic, event->topic_len, event->data, event->data_len,
                                  event->current_data_offset, event->total_data_len, event->retain) && s_message_cb) {
                s_message_cb(s_rx.topic, s_rx.payload, s_rx.retained, s_connected_ctx);
            }
            break;
        }
        default:
            break;
    }
}

esp_err_t mqtt_app_start(mqtt_connected_cb_t on_connected, mqtt_message_cb_t on_message, void *user_ctx) {
    const app_config_t *config = app_config_get();
    const bool tls = strncmp(config->mqtt_uri, "mqtts://", 8) == 0;
    ESP_RETURN_ON_FALSE(tls || (!config->mqtt_require_tls && strncmp(config->mqtt_uri, "mqtt://", 7) == 0),
                        ESP_ERR_INVALID_ARG, TAG, "MQTT requires mqtts:// unless require_tls is explicitly disabled");
    snprintf(s_availability_topic, sizeof(s_availability_topic), "%s/status", config->base_topic);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config->mqtt_uri,
        .broker.verification.certificate = tls && config->mqtt_ca_certificate[0] ? config->mqtt_ca_certificate : NULL,
        .broker.verification.crt_bundle_attach = tls && !config->mqtt_ca_certificate[0] ? esp_crt_bundle_attach : NULL,
        .session.last_will = {.topic = s_availability_topic, .msg = "offline", .qos = 1, .retain = true},
        .credentials.username = config->mqtt_username,
        .credentials.authentication.password = config->mqtt_password,
    };

    s_connected_cb = on_connected;
    s_message_cb = on_message;
    s_connected_ctx = user_ctx;
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    s_tx_queue = xQueueCreate(MQTT_TX_QUEUE_DEPTH, sizeof(mqtt_tx_request_t));
    if (s_tx_queue == NULL || xTaskCreate(mqtt_tx_task, "mqtt_tx", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

esp_mqtt_client_handle_t mqtt_app_client(void) {
    return s_client;
}

esp_err_t mqtt_app_publish(const char *topic, const char *payload, bool retain) {
    ESP_RETURN_ON_FALSE(topic != NULL, ESP_ERR_INVALID_ARG, TAG, "topic is null");
    ESP_RETURN_ON_FALSE(payload != NULL, ESP_ERR_INVALID_ARG, TAG, "payload is null");
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_INVALID_STATE, TAG, "mqtt client not started");

    if (esp_mqtt_client_publish(s_client, topic, payload, 0, 1, retain) < 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mqtt_app_publish_async(const char *topic, const char *payload, bool retain) {
    ESP_RETURN_ON_FALSE(topic != NULL && payload != NULL, ESP_ERR_INVALID_ARG, TAG, "null queued MQTT value");
    ESP_RETURN_ON_FALSE(s_tx_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT queue not started");
    const size_t topic_len = strlen(topic), payload_len = strlen(payload);
    ESP_RETURN_ON_FALSE(topic_len < MQTT_TX_TOPIC_MAX_LEN && payload_len < MQTT_TX_PAYLOAD_MAX_LEN,
                        ESP_ERR_INVALID_SIZE, TAG, "queued MQTT payload too large");
    mqtt_tx_request_t request = {.retain = retain};
    memcpy(request.topic, topic, topic_len + 1);
    memcpy(request.payload, payload, payload_len + 1);
    return xQueueSend(s_tx_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
