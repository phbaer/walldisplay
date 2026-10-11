"""Capture must survive overwriting the caller's temporary board structure."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_capture_uses_persistent_display_handle(tmp_path):
    source = (ROOT / 'src/screenshot.c').read_text()
    declaration = re.search(r'static (?:const display_board_handle_t \*s_board|lv_display_t \*s_display);', source).group()
    initializer = re.search(r'    (?:s_board = board|s_display = board->display);', source).group()
    capture = source[source.index('static esp_err_t capture_frame('):source.index('static void screenshot_task(')]
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define BOARD_LCD_WIDTH 2
#define BOARD_LCD_HEIGHT 2
#define SCREENSHOT_BYTES_PER_PIXEL 2U
#define SCREENSHOT_NAME_MAX 32
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_TIMEOUT 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0
typedef int esp_err_t;
typedef struct { int sentinel; } lv_display_t;
typedef struct { lv_display_t *display; } display_board_handle_t;
typedef struct { struct { unsigned w, h, stride; } header; uint8_t *data; } lv_draw_buf_t;
static lv_display_t display;
static uint8_t pixels[8] = {1,2,3,4,5,6,7,8};
static lv_draw_buf_t draw_buffer = {{2,2,4}, pixels};
static atomic_bool s_enabled = true;
static void *s_storage_lock;
static char s_saved_names[4][SCREENSHOT_NAME_MAX];
static size_t s_next_saved;
static void *heap_caps_malloc(size_t size, int caps) { (void)caps; return malloc(size); }
static bool lvgl_port_lock(int timeout) { (void)timeout; return true; }
static void lvgl_port_unlock(void) {}
static lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *handle) {
    assert(handle == &display); /* A freed/overwritten startup structure must not be dereferenced. */
    return &draw_buffer;
}
static void xSemaphoreTake(void *lock, int timeout) { (void)lock; (void)timeout; }
static void xSemaphoreGive(void *lock) { (void)lock; }
static void screenshot_path(char *dest, size_t size, const char *name, const char *ext) {
    (void)ext; snprintf(dest, size, "%s", name);
}
static int unlink(const char *path) { (void)path; return 0; }
static size_t strlcpy(char *dest, const char *src, size_t size) {
    size_t len = strlen(src); assert(len < size); memcpy(dest, src, len+1); return len;
}
static esp_err_t write_bmp(const uint8_t *frame, uint32_t stride, const char *name, const char **stage) {
    (void)stage; assert(stride == 4); assert(strcmp(name, "test") == 0);
    assert(memcmp(frame, pixels, sizeof(pixels)) == 0); return ESP_OK;
}
'''
    code += declaration + '\nstatic void initialize(const display_board_handle_t *board) {\n' + initializer + '\n}\n' + capture
    code += r'''
int main(void) {
    display_board_handle_t board = {&display};
    initialize(&board);
    board.display = (lv_display_t *)1; /* Reuse the caller's former stack storage. */
    const char *stage = NULL;
    assert(capture_frame("test", &stage) == ESP_OK);
    return 0;
}
'''
    c_file = tmp_path / 'capture.c'
    c_file.write_text(code)
    executable = tmp_path / 'capture'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c_file), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
