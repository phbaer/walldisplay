"""Run storage fault tests against the exact LittleFS core used by firmware."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_littlefs_replacement_and_power_loss(tmp_path):
    core = ROOT / "managed_components/joltwallet__littlefs/src/littlefs"
    assert (core / "lfs.c").exists(), "Run idf.py build to download the pinned LittleFS dependency"
    executable = tmp_path / "littlefs-test"
    subprocess.run([
        "cc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
        "-DLFS_NO_DEBUG", "-DLFS_NO_WARN", "-DLFS_NO_ERROR", "-I", str(core),
        str(ROOT / "tests/test_littlefs_storage.c"), str(core / "lfs.c"),
        str(core / "lfs_util.c"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)


def test_firmware_bmp_writer_with_littlefs(tmp_path):
    source = (ROOT / "src/screenshot.c").read_text()
    helpers = source[source.index("static void screenshot_path("):source.index("static void publish_status(")]
    writer = source[source.index("static esp_err_t write_bmp("):source.index("static esp_err_t capture_frame(")]
    harness = r'''
#define main filesystem_fault_tests
#include "test_littlefs_storage.c"
#undef main
#include <stdlib.h>
#define BOARD_LCD_WIDTH 480
#define BOARD_LCD_HEIGHT 480
#define SCREENSHOT_NAME_MAX 32
#define SCREENSHOT_BMP_HEADER_SIZE 54U
#define SCREENSHOT_BMP_BYTES_PER_PIXEL 3U
#define SCREENSHOT_MOUNT_PATH "."
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
static lfs_t writer_fs;
static int opened;
typedef struct {
    lfs_file_t file;
    struct lfs_file_config config;
    uint8_t cache[512];
} capture_file;
static capture_file *capture_open(const char *path, const char *mode) {
    assert(strcmp(mode, "wb") == 0);
    capture_file *file = calloc(1, sizeof(*file));
    assert(file != NULL);
    file->config.buffer = file->cache;
    assert(lfs_file_opencfg(&writer_fs, &file->file, path + 2,
        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &file->config) == 0);
    opened++;
    return file;
}
static size_t capture_write(const void *buf, size_t size, size_t count, capture_file *file) {
    int n = lfs_file_write(&writer_fs, &file->file, buf, size * count);
    return n < 0 ? 0 : (size_t)n / size;
}
static int capture_close(capture_file *file) {
    int n = lfs_file_close(&writer_fs, &file->file);
    opened--;
    free(file);
    return n;
}
static int capture_rename(const char *src, const char *dst) {
    assert(opened == 0); /* ESP-IDF VFS rejects renaming open files. */
    return lfs_rename(&writer_fs, src + 2, dst + 2);
}
static int capture_unlink(const char *path) {
    assert(strstr(path, ".bmp") == NULL); /* Never delete the old capture first. */
    return lfs_remove(&writer_fs, path + 2);
}
#define FILE capture_file
#define fopen capture_open
#define fwrite capture_write
#define fclose capture_close
#define rename capture_rename
#define unlink capture_unlink
'''
    harness += helpers + writer + r'''
#undef FILE
#undef fopen
#undef fwrite
#undef fclose
#undef rename
#undef unlink
static void check_bmp(uint8_t expected) {
    struct lfs_info info;
    assert(lfs_stat(&writer_fs, "homepage.bmp", &info) == 0 && info.size == BMP_SIZE);
    lfs_file_t file;
    assert(lfs_file_open(&writer_fs, &file, "homepage.bmp", LFS_O_RDONLY) == 0);
    uint8_t header[54], pixel[3];
    assert(lfs_file_read(&writer_fs, &file, header, sizeof(header)) == sizeof(header));
    assert(header[0] == 'B' && header[1] == 'M');
    assert(lfs_file_read(&writer_fs, &file, pixel, sizeof(pixel)) == sizeof(pixel));
    for (unsigned i = 0; i < sizeof(pixel); ++i) assert(pixel[i] == expected);
    assert(lfs_file_close(&writer_fs, &file) == 0);
    assert(lfs_stat(&writer_fs, "homepage.tmp", &info) == LFS_ERR_NOENT);
}
int main(void) {
    memset(flash, 255, sizeof(flash));
    assert(lfs_format(&writer_fs, &config) == 0);
    assert(lfs_mount(&writer_fs, &config) == 0);
    static uint8_t frame[480 * 480 * 2];
    const char *stage = NULL;
    assert(write_bmp(frame, 960, "homepage", &stage) == ESP_OK);
    check_bmp(0);
    memset(frame, 255, sizeof(frame));
    assert(write_bmp(frame, 960, "homepage", &stage) == ESP_OK);
    check_bmp(255);
    assert(lfs_unmount(&writer_fs) == 0);
    return 0;
}
'''
    c_file = tmp_path / "writer.c"
    c_file.write_text(harness)
    core = ROOT / "managed_components/joltwallet__littlefs/src/littlefs"
    executable = tmp_path / "writer"
    subprocess.run([
        "cc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
        "-DLFS_NO_DEBUG", "-DLFS_NO_WARN", "-DLFS_NO_ERROR",
        "-I", str(core), "-I", str(ROOT / "tests"), str(c_file),
        str(core / "lfs.c"), str(core / "lfs_util.c"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
