#include "walldisplay/panel_topics.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char seen[64][48];
static size_t seen_count;

static void collect(const char *suffix, void *context) {
    assert(context == seen && seen_count < 64 && strlen(suffix) < sizeof(seen[0]));
    strcpy(seen[seen_count++], suffix);
}

static bool subscribed(const char *suffix) {
    for (size_t i = 0; i < seen_count; ++i) if (strcmp(seen[i], suffix) == 0) return true;
    return false;
}

int main(void) {
    assert(strcmp(panel_topic_suffix("panel/kitchen", "panel/kitchen/set/weather"), "set/weather") == 0);
    /* A sibling panel sharing the prefix is foreign. */
    assert(panel_topic_suffix("panel/kitchen", "panel/kitchen2/set/weather") == NULL);
    assert(panel_topic_suffix("panel/kitchen", "panel/kitchen") == NULL);
    assert(panel_topic_suffix("panel/kitchen", "homeassistant/status") == NULL);
    assert(panel_topic_suffix("", "/set/weather") == NULL);
    assert(panel_topic_suffix(NULL, "x") == NULL && panel_topic_suffix("x", NULL) == NULL);

    size_t index = 99;
    assert(panel_topic_index("set/grid/6", "set/grid/", "", 6, &index) && index == 5);
    assert(!panel_topic_index("set/grid/7", "set/grid/", "", 6, &index));
    assert(!panel_topic_index("set/grid/0", "set/grid/", "", 6, &index));
    assert(!panel_topic_index("set/grid/", "set/grid/", "", 6, &index));
    assert(!panel_topic_index("set/grid/12", "set/grid/", "", 6, &index));
    assert(!panel_topic_index("set/grid/1/x", "set/grid/", "", 6, &index));
    assert(!panel_topic_index("set/grid/1", "set/grid/", "", 10, &index));

    bool canonical = false;
    assert(panel_topic_input("set/media/power", "media/power", &canonical) && canonical);
    assert(panel_topic_input("state/media/power", "media/power", &canonical) && !canonical);
    assert(!panel_topic_input("cmd/media/power", "media/power", &canonical));
    assert(!panel_topic_input("set/media", "media/power", &canonical));
    assert(!panel_topic_input("settings/media", "tings/media", &canonical));
    assert(panel_topic_input_index("set/button3/label", "button", "/label", 5, &index, &canonical) && index == 2 && canonical);
    assert(panel_topic_input_index("state/chip4", "chip", "", 4, &index, &canonical) && index == 3 && !canonical);
    assert(!panel_topic_input_index("state/chip4/color", "chip", "", 4, &index, &canonical));
    assert(panel_topic_input_index("state/chip4/color", "chip", "/color", 4, &index, &canonical));
    assert(!panel_topic_input_index("set/button6/state", "button", "/state", 5, &index, &canonical));

    panel_topics_for_each_subscription(collect, seen);
    /* Two wildcards, seven plain aliases, 5+5+5+5+4+4 indexed aliases. */
    assert(seen_count == 2 + 7 + 28);
    assert(subscribed("set/#") && subscribed("cmd/#") && !subscribed("state/#"));
    assert(subscribed("state/weather") && subscribed("state/media/power") && subscribed("state/display_power"));
    assert(subscribed("state/media/favorite5/icon") && subscribed("state/button1/state"));
    assert(subscribed("state/chip4") && subscribed("state/chip4/color") && !subscribed("state/chip5"));
    /* Panel-published state must not be subscribed, or it would echo back. */
    assert(!subscribed("state/device") && !subscribed("state/update") && !subscribed("state/hostname"));
    puts("Panel topic tests passed");
    return 0;
}
