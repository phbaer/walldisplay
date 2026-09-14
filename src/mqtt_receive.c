#include "walldisplay/mqtt_receive.h"
#include <string.h>
#include "walldisplay/security_policy.h"
bool mqtt_receive_feed(mqtt_receive_t *rx, const char *topic, size_t topic_len,
                       const char *data, size_t length, size_t offset, size_t total, bool retained) {
    if (offset == 0) {
        rx->active = false;
        if (!topic || !topic_len || topic_len >= sizeof(rx->topic) || total >= sizeof(rx->payload) || memchr(topic, 0, topic_len)) return false;
        memcpy(rx->topic, topic, topic_len);
        rx->topic[topic_len] = 0;
        rx->total = total;
        rx->received = 0;
        rx->retained = retained;
        rx->active = true;
    }
    if (!rx->active || offset != rx->received || total != rx->total || length > total - rx->received ||
        (length && (!data || memchr(data, 0, length)))) {
        rx->active = false;
        return false;
    }
    if (length) memcpy(rx->payload + offset, data, length);
    rx->received += length;
    if (rx->received != total) return false;
    rx->payload[total] = 0;
    rx->active = false;
    return json_depth_safe(rx->payload);
}
