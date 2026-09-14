#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>
static size_t test_strlcpy(char *dest, const char *source, size_t size) {
    size_t length = strlen(source);
    if (size) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dest, source, copied);
        dest[copied] = 0;
    }
    return length;
}
#define strlcpy test_strlcpy
#include "../src/app_config.c"
static char saved[PANEL_LAYOUT_JSON_SIZE], pending[PANEL_LAYOUT_JSON_SIZE];
static int writes;
static bool fail_commit;
esp_err_t nvs_flash_init_partition(const char *name) { (void)name; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle=1; return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p, const char *n, int m, nvs_handle_t *h) { (void)p; (void)n; (void)m; (void)h; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *size) {
    (void)h;
    if (strcmp(key,"pages") || !saved[0]) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size > strlen(saved)); strcpy(out,saved); return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) { (void)h; (void)key; (void)value; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) {
    (void)h; assert(!strcmp(key,"pages")); strcpy(pending,value); ++writes; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; if (fail_commit) return ESP_FAIL; strcpy(saved,pending); return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; pending[0]=0; }
int main(void) {
    const char *buttons="{\"pages\":[\"buttons\"],\"default_page\":\"buttons\"}";
    assert(app_config_init()==ESP_OK);
    assert(app_config_set_pages(buttons)==ESP_OK && writes==1);
    assert(app_config_set_pages(buttons)==ESP_OK && writes==1);
    s_initialized=false; /* simulate restart with persistent NVS backend */
    assert(app_config_init()==ESP_OK);
    assert(app_config_get()->layout.count==1 && app_config_get()->default_page==APP_DEFAULT_PAGE_BUTTONS);
    fail_commit=true;
    assert(app_config_set_pages(APPCFG_DEFAULT_LAYOUT_JSON)==ESP_FAIL);
    assert(app_config_get()->default_page==APP_DEFAULT_PAGE_BUTTONS);
    s_initialized=false;
    assert(app_config_init()==ESP_OK && app_config_get()->default_page==APP_DEFAULT_PAGE_BUTTONS);
    assert(app_config_set_pages("invalid")==ESP_ERR_INVALID_ARG);
}
