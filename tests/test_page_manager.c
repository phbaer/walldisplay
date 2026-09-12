#include "walldisplay/page_manager.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    panel_layout_t layout;
    panel_layout_defaults(&layout);
    assert(layout.count == 2);
    assert(panel_layout_next(&layout, PANEL_PAGE_WEATHER) == PANEL_PAGE_MEDIA);
    assert(panel_layout_next(&layout, PANEL_PAGE_MEDIA) == PANEL_PAGE_WEATHER);
    panel_page_id_t legacy;
    assert(panel_page_parse("MEDIA", &legacy) && legacy == PANEL_PAGE_MEDIA);
    const char *invalid[] = {
        "{}", "null", "[]", "{broken",
        "{\"pages\":[],\"default_page\":\"weather\"}",
        "{\"pages\":[\"weather\",\"weather\"],\"default_page\":\"weather\"}",
        "{\"pages\":[\"buttons\"],\"default_page\":\"weather\"}",
        "{\"pages\":[\"unknown\"],\"default_page\":\"unknown\"}",
        "{\"pages\":[1],\"default_page\":\"weather\"}",
        "{\"pages\":[\"weather\"],\"default_page\":\"weather\",\"titles\":{\"weather\":1}}",
        "{\"pages\":[\"weather\"],\"default_page\":\"weather\"} trailing",
    };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        panel_layout_t before = layout;
        assert(!panel_layout_parse(invalid[i], &layout));
        assert(memcmp(&before, &layout, sizeof(layout)) == 0);
    }
    assert(panel_layout_parse("{\"pages\":[\"buttons\",\"weather\",\"media\"],\"default_page\":\"buttons\",\"titles\":{\"buttons\":\"Living room\"}}", &layout));
    assert(layout.count == 3 && layout.default_page == PANEL_PAGE_BUTTONS);
    assert(panel_layout_next(&layout, PANEL_PAGE_MEDIA) == PANEL_PAGE_BUTTONS);
    char json[PANEL_LAYOUT_JSON_SIZE];
    assert(panel_layout_json(&layout, json, sizeof(json)));
    panel_layout_t roundtrip;
    assert(panel_layout_parse(json, &roundtrip));
    assert(strcmp(roundtrip.titles[PANEL_PAGE_BUTTONS], "Living room") == 0);
    assert(roundtrip.order[0] == PANEL_PAGE_BUTTONS);
    assert(panel_layout_parse("{\"pages\":[\"buttons\"],\"default_page\":\"buttons\"}", &layout));
    assert(panel_layout_next(&layout, PANEL_PAGE_BUTTONS) == PANEL_PAGE_BUTTONS);
    assert(!panel_layout_contains(&layout, PANEL_PAGE_MEDIA));
    puts("Page manager tests passed");
}
