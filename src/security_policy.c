#include "walldisplay/security_policy.h"
#include <string.h>
bool screenshot_authorized(const char *token, const char *header) {
    if (!token || !token[0] || !header || strncmp(header, "Bearer ", 7)) return false;
    const char *value = header + 7;
    size_t length = strlen(token);
    if (strlen(value) != length) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= (unsigned char)value[i] ^ (unsigned char)token[i];
    return difference == 0;
}
bool mqtt_replayed_command(const char *base, const char *topic, bool retained) {
    if (!base || !topic || !retained) return false;
    size_t length = strlen(base);
    return strncmp(base, topic, length) == 0 && strncmp(topic + length, "/cmd/", 5) == 0;
}

bool json_depth_safe(const char *text) {
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    if (!text) return false;
    for (; *text; ++text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*text == '\\') escaped = true;
            else if (*text == '"') quoted = false;
        } else if (*text == '"') quoted = true;
        else if (*text == '{' || *text == '[') {
            if (++depth > 16) return false;
        } else if ((*text == '}' || *text == ']') && depth) --depth;
    }
    return true;
}

bool update_manifest_url_valid(const char *url) {
    static const char prefix[] = "https://";
    const size_t prefix_length = sizeof(prefix) - 1;
    if (!url || strncmp(url, prefix, prefix_length) != 0) return false;
    for (const char *p = url; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c >= 0x7f) return false;
    }
    const char *host = url + prefix_length;
    size_t host_length = 0;
    while (host[host_length] != '\0' && host[host_length] != '/') {
        const char c = host[host_length];
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                             c == '.' || c == '-' || c == '[' || c == ']' || c == ':';
        if (!allowed) return false;
        ++host_length;
    }
    if (host_length == 0 || host_length > 253 || host[host_length] != '/') return false;
    return host[host_length + 1] != '\0';
}
