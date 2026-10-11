"""Exercise the firmware BMP writer with SPIFFS destination-conflict semantics."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_repeated_named_capture_replaces_bmp(tmp_path):
    source = (ROOT / "src/screenshot.c").read_text()
    helpers = source[source.index("static void screenshot_path("):source.index("static void publish_status(")]
    writer = source[source.index("static esp_err_t write_bmp("):source.index("static esp_err_t capture_frame(")]
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#define BOARD_LCD_WIDTH 2
#define BOARD_LCD_HEIGHT 2
#define SCREENSHOT_NAME_MAX 32
#define SCREENSHOT_BMP_HEADER_SIZE 54U
#define SCREENSHOT_BMP_BYTES_PER_PIXEL 3U
#define SCREENSHOT_MOUNT_PATH "."
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
static int spiffs_rename(const char *src, const char *dst) {
    if (access(dst, F_OK) == 0) { errno = EEXIST; return -1; }
    return rename(src, dst);
}
#define rename spiffs_rename
'''
    harness += helpers + writer + r'''
int main(void) {
    uint8_t frame[8] = {0};
    const char *stage = NULL;
    assert(write_bmp(frame, 4, "homepage", &stage) == ESP_OK);
    memset(frame, 255, sizeof(frame));
    assert(write_bmp(frame, 4, "homepage", &stage) == ESP_OK);
    FILE *file = fopen("homepage.bmp", "rb");
    assert(file != NULL);
    assert(fseek(file, 54, SEEK_SET) == 0);
    assert(fgetc(file) == 255);
    assert(fclose(file) == 0);
    assert(access("homepage.tmp", F_OK) != 0);
    return 0;
}
'''
    c_file = tmp_path / "writer.c"
    c_file.write_text(harness)
    executable = tmp_path / "writer"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=tmp_path, check=True)
