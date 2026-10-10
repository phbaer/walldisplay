/* Exercise the pinned LittleFS core on a NOR flash simulator, including power cuts. */
#include "lfs.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 4096
#define BLOCK_COUNT (0x6D0000 / BLOCK_SIZE)
#define BMP_SIZE 691254
static uint8_t flash[BLOCK_SIZE * BLOCK_COUNT], baseline[sizeof(flash)];
static uint8_t read_cache[512], prog_cache[512], lookahead[128], file_cache[512];
static jmp_buf power_loss;
static int cut_after = -1, operations;
static int read_flash(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *buf, lfs_size_t n) {
    (void)c;
    assert(b < BLOCK_COUNT && o + n <= BLOCK_SIZE);
    memcpy(buf, flash + b * BLOCK_SIZE + o, n);
    return 0;
}
static int program_flash(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *buf, lfs_size_t n) {
    (void)c;
    assert(b < BLOCK_COUNT && o + n <= BLOCK_SIZE);
    uint8_t *dst = flash + b * BLOCK_SIZE + o;
    const uint8_t *src = buf;
    int interrupted = operations++ == cut_after;
    for (lfs_size_t i = 0; i < (interrupted ? n / 2 : n); ++i) {
        assert((dst[i] & src[i]) == src[i]);
        dst[i] &= src[i];
    }
    if (interrupted) longjmp(power_loss, 1);
    return 0;
}
static int erase_flash(const struct lfs_config *c, lfs_block_t b) {
    (void)c;
    assert(b < BLOCK_COUNT);
    int interrupted = operations++ == cut_after;
    memset(flash + b * BLOCK_SIZE, 255, interrupted ? BLOCK_SIZE / 2 : BLOCK_SIZE);
    if (interrupted) longjmp(power_loss, 1);
    return 0;
}
static int sync_flash(const struct lfs_config *c) { (void)c; return 0; }
static const struct lfs_config config = {
    .read = read_flash, .prog = program_flash, .erase = erase_flash, .sync = sync_flash,
    .read_size = 128, .prog_size = 128, .block_size = BLOCK_SIZE, .block_count = BLOCK_COUNT,
    .block_cycles = 512, .cache_size = 512, .lookahead_size = 128,
    .read_buffer = read_cache, .prog_buffer = prog_cache, .lookahead_buffer = lookahead,
};
static void write_capture(lfs_t *fs, const char *name, uint8_t value) {
    lfs_file_t file;
    const struct lfs_file_config fc = {.buffer = file_cache};
    assert(lfs_file_opencfg(fs, &file, name, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc) == 0);
    uint8_t row[1440];
    memset(row, value, sizeof(row));
    for (int written = 0; written < BMP_SIZE;) {
        int n = BMP_SIZE - written;
        if (n > (int)sizeof(row)) n = sizeof(row);
        assert(lfs_file_write(fs, &file, row, n) == n);
        written += n;
    }
    assert(lfs_file_close(fs, &file) == 0);
}
static uint8_t verify_capture(lfs_t *fs) {
    lfs_file_t file;
    const struct lfs_file_config fc = {.buffer = file_cache};
    assert(lfs_file_opencfg(fs, &file, "homepage.bmp", LFS_O_RDONLY, &fc) == 0);
    assert(lfs_file_size(fs, &file) == BMP_SIZE);
    uint8_t buf[1440], value = 0;
    int total = 0;
    for (;;) {
        int n = lfs_file_read(fs, &file, buf, sizeof(buf));
        assert(n >= 0);
        if (n == 0) break;
        if (total == 0) value = buf[0];
        for (int i = 0; i < n; ++i) assert(buf[i] == value);
        total += n;
    }
    assert(total == BMP_SIZE);
    assert(lfs_file_close(fs, &file) == 0);
    return value;
}
static void recover(uint8_t expected, int either) {
    cut_after = -1;
    lfs_t fs = {0};
    assert(lfs_mount(&fs, &config) == 0);
    uint8_t value = verify_capture(&fs);
    assert(value == expected || (either && value == 0x22));
    /* After recovery, the same filename remains replaceable. */
    write_capture(&fs, "homepage.tmp", 0x33);
    assert(lfs_rename(&fs, "homepage.tmp", "homepage.bmp") == 0);
    assert(verify_capture(&fs) == 0x33);
    assert(lfs_unmount(&fs) == 0);
}
int main(void) {
    memset(flash, 255, sizeof(flash));
    lfs_t fs = {0};
    assert(lfs_mount(&fs, &config) < 0); /* Blank/foreign filesystem requires formatting. */
    assert(lfs_format(&fs, &config) == 0);
    assert(lfs_mount(&fs, &config) == 0);
    write_capture(&fs, "homepage.bmp", 0x11);
    write_capture(&fs, "homepage.tmp", 0x22);
    assert(lfs_unmount(&fs) == 0);
    memcpy(baseline, flash, sizeof(flash));
    assert(lfs_mount(&fs, &config) == 0);
    operations = 0;
    assert(lfs_rename(&fs, "homepage.tmp", "homepage.bmp") == 0);
    int rename_ops = operations;
    assert(verify_capture(&fs) == 0x22);
    assert(lfs_unmount(&fs) == 0);
    assert(rename_ops > 0);
    for (int cut = 0; cut < rename_ops; ++cut) {
        memcpy(flash, baseline, sizeof(flash));
        memset(&fs, 0, sizeof(fs));
        assert(lfs_mount(&fs, &config) == 0);
        operations = 0;
        cut_after = cut;
        if (setjmp(power_loss) == 0) { (void)lfs_rename(&fs, "homepage.tmp", "homepage.bmp"); assert(0); }
        recover(0x11, 1);
    }
    const int write_cuts[] = {0, 1, 8, 64, 256};
    for (unsigned i = 0; i < sizeof(write_cuts) / sizeof(write_cuts[0]); ++i) {
        memcpy(flash, baseline, sizeof(flash));
        memset(&fs, 0, sizeof(fs));
        assert(lfs_mount(&fs, &config) == 0);
        operations = 0;
        cut_after = write_cuts[i];
        if (setjmp(power_loss) == 0) { write_capture(&fs, "homepage.tmp", 0x22); assert(0); }
        recover(0x11, 0);
    }
    puts("LittleFS replacement and interrupted-write recovery passed");
    return 0;
}
