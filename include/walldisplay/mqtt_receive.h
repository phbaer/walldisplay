#pragma once
#include <stdbool.h>
#include <stddef.h>
#define MQTT_RX_TOPIC_SIZE 256
#define MQTT_RX_PAYLOAD_SIZE 2048

typedef struct {
    char topic[MQTT_RX_TOPIC_SIZE];
    char payload[MQTT_RX_PAYLOAD_SIZE];
    size_t total, received;
    bool active, retained;
} mqtt_receive_t;
/* Returns true only for a complete, contiguous, text message. */
bool mqtt_receive_feed(mqtt_receive_t *rx, const char *topic, size_t topic_len,
                       const char *data, size_t length, size_t offset, size_t total, bool retained);
