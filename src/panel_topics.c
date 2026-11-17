#include "walldisplay/panel_topics.h"

#include <stdio.h>
#include <string.h>

const char *panel_topic_suffix(const char *base, const char *topic) {
    if (base == NULL || topic == NULL) return NULL;
    const size_t length = strlen(base);
    if (length == 0 || strncmp(topic, base, length) != 0 || topic[length] != '/') return NULL;
    return topic + length + 1;
}

bool panel_topic_index(const char *suffix, const char *prefix, const char *tail, size_t count, size_t *index) {
    if (suffix == NULL || count == 0 || count > 9) return false;
    const size_t prefix_length = strlen(prefix);
    if (strncmp(suffix, prefix, prefix_length) != 0) return false;
    const char digit = suffix[prefix_length];
    if (digit < '1' || digit > (char)('0' + count) || strcmp(suffix + prefix_length + 1, tail) != 0) return false;
    *index = (size_t)(digit - '1');
    return true;
}

/* Strip "set/" or "state/" and report which one it was. */
static const char *input_name(const char *suffix, bool *canonical) {
    if (suffix == NULL) return NULL;
    if (strncmp(suffix, "set/", 4) == 0) { *canonical = true; return suffix + 4; }
    if (strncmp(suffix, "state/", 6) == 0) { *canonical = false; return suffix + 6; }
    return NULL;
}

bool panel_topic_input(const char *suffix, const char *name, bool *canonical) {
    bool set = false;
    const char *input = input_name(suffix, &set);
    if (input == NULL || strcmp(input, name) != 0) return false;
    *canonical = set;
    return true;
}

bool panel_topic_input_index(const char *suffix, const char *prefix, const char *tail, size_t count,
                             size_t *index, bool *canonical) {
    bool set = false;
    const char *input = input_name(suffix, &set);
    if (input == NULL || !panel_topic_index(input, prefix, tail, count, index)) return false;
    *canonical = set;
    return true;
}

/* Inputs that also accept a retained "state/<name>" alias. */
static const char *const s_state_aliases[] = {
    "weather", "media", "media/power", "name", "clock", "date", "display_power",
};

typedef struct { const char *prefix; const char *tail; size_t count; } indexed_alias_t;
static const indexed_alias_t s_indexed_state_aliases[] = {
    {"media/favorite", "/label", PANEL_MEDIA_FAVORITE_COUNT},
    {"media/favorite", "/icon", PANEL_MEDIA_FAVORITE_COUNT},
    {"button", "/label", PANEL_FOOTER_BUTTON_COUNT},
    {"button", "/state", PANEL_FOOTER_BUTTON_COUNT},
    {"chip", "", PANEL_CHIP_COUNT},
    {"chip", "/color", PANEL_CHIP_COUNT},
};

void panel_topics_for_each_subscription(panel_topic_visit_t visit, void *context) {
    char suffix[48];
    visit("set/#", context);
    visit("cmd/#", context);
    for (size_t i = 0; i < sizeof(s_state_aliases) / sizeof(s_state_aliases[0]); ++i) {
        snprintf(suffix, sizeof(suffix), "state/%s", s_state_aliases[i]);
        visit(suffix, context);
    }
    for (size_t i = 0; i < sizeof(s_indexed_state_aliases) / sizeof(s_indexed_state_aliases[0]); ++i) {
        const indexed_alias_t *alias = &s_indexed_state_aliases[i];
        for (size_t n = 1; n <= alias->count; ++n) {
            snprintf(suffix, sizeof(suffix), "state/%s%u%s", alias->prefix, (unsigned)n, alias->tail);
            visit(suffix, context);
        }
    }
}
