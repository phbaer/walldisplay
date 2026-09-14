#pragma once
#include <stdbool.h>
#include <stddef.h>

#define PANEL_PAGE_COUNT 4
#define PANEL_MAX_PAGES 5
#define PANEL_PAGE_TITLE_SIZE 49
#define PANEL_LAYOUT_JSON_SIZE 1024

typedef enum { PANEL_PAGE_WEATHER, PANEL_PAGE_MEDIA, PANEL_PAGE_BUTTONS, PANEL_PAGE_ABOUT } panel_page_id_t;
typedef struct {
    panel_page_id_t order[PANEL_MAX_PAGES];
    size_t count;
    panel_page_id_t default_page;
    char titles[PANEL_PAGE_COUNT][PANEL_PAGE_TITLE_SIZE];
} panel_layout_t;

void panel_layout_defaults(panel_layout_t *layout);
const char *panel_page_name(panel_page_id_t id);
bool panel_page_parse(const char *name, panel_page_id_t *id);
bool panel_layout_contains(const panel_layout_t *layout, panel_page_id_t id);
panel_page_id_t panel_layout_next(const panel_layout_t *layout, panel_page_id_t current);
/* Parse into a temporary value: invalid input never modifies the destination. */
bool panel_layout_parse(const char *json, panel_layout_t *layout);
bool panel_layout_json(const panel_layout_t *layout, char *buffer, size_t size);
