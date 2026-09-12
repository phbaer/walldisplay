#include "walldisplay/page_manager.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>

static const char *const names[] = {"weather", "media", "buttons"};
void panel_layout_defaults(panel_layout_t *layout) {
    *layout = (panel_layout_t){.order = {PANEL_PAGE_WEATHER, PANEL_PAGE_MEDIA}, .count = 2,
        .default_page = PANEL_PAGE_WEATHER, .titles = {"Weather", "Media", "Buttons"}};
}
const char *panel_page_name(panel_page_id_t id) {
    return (unsigned)id < PANEL_PAGE_COUNT ? names[id] : "";
}
bool panel_page_parse(const char *name, panel_page_id_t *id) {
    if (!name) return false;
    for (size_t i = 0; i < PANEL_PAGE_COUNT; ++i) {
        if (strcasecmp(name, names[i]) == 0) { *id = (panel_page_id_t)i; return true; }
    }
    return false;
}
bool panel_layout_contains(const panel_layout_t *layout, panel_page_id_t id) {
    for (size_t i = 0; i < layout->count; ++i) if (layout->order[i] == id) return true;
    return false;
}
panel_page_id_t panel_layout_next(const panel_layout_t *layout, panel_page_id_t current) {
    for (size_t i = 0; i < layout->count; ++i)
        if (layout->order[i] == current) return layout->order[(i + 1) % layout->count];
    return layout->default_page;
}
bool panel_layout_parse(const char *json, panel_layout_t *layout) {
    if (!json || strlen(json) >= PANEL_LAYOUT_JSON_SIZE) return false;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    bool valid = false;
    panel_layout_t candidate;
    panel_layout_defaults(&candidate);
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    cJSON *initial = cJSON_GetObjectItemCaseSensitive(root, "default_page");
    cJSON *titles = cJSON_GetObjectItemCaseSensitive(root, "titles");
    if (!cJSON_IsObject(root) || !cJSON_IsArray(pages) || !cJSON_IsString(initial) ||
        !panel_page_parse(initial->valuestring, &candidate.default_page)) goto done;
    int count = cJSON_GetArraySize(pages);
    if (count < 1 || count > PANEL_PAGE_COUNT) goto done;
    candidate.count = 0;
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(pages, i);
        panel_page_id_t id;
        if (!cJSON_IsString(item) || !panel_page_parse(item->valuestring, &id) ||
            panel_layout_contains(&candidate, id)) goto done;
        candidate.order[candidate.count++] = id;
    }
    if (!panel_layout_contains(&candidate, candidate.default_page)) goto done;
    if (titles) {
        if (!cJSON_IsObject(titles)) goto done;
        cJSON *title;
        cJSON_ArrayForEach(title, titles) {
            panel_page_id_t id;
            if (!panel_page_parse(title->string, &id) || !cJSON_IsString(title) ||
                strlen(title->valuestring) >= PANEL_PAGE_TITLE_SIZE) goto done;
            strcpy(candidate.titles[id], title->valuestring);
        }
    }
    *layout = candidate;
    valid = true;
done:
    cJSON_Delete(root);
    return valid;
}
bool panel_layout_json(const panel_layout_t *layout, char *buffer, size_t size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *pages = cJSON_AddArrayToObject(root, "pages");
    cJSON *titles = cJSON_AddObjectToObject(root, "titles");
    bool valid = root && pages && titles;
    for (size_t i = 0; valid && i < layout->count; ++i)
        valid = cJSON_AddItemToArray(pages, cJSON_CreateString(panel_page_name(layout->order[i])));
    for (size_t i = 0; valid && i < PANEL_PAGE_COUNT; ++i)
        valid = cJSON_AddStringToObject(titles, names[i], layout->titles[i]) != NULL;
    valid = valid && cJSON_AddStringToObject(root, "default_page", panel_page_name(layout->default_page));
    valid = valid && cJSON_PrintPreallocated(root, buffer, (int)size, false);
    cJSON_Delete(root);
    return valid;
}
