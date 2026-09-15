/* Cronix OS · vfs.c: path split + pretty listing on top of FAT. */
#include "vfs.h"

#include "fat.h"
#include "heap.h"
#include "print.h"

#define ATTR_DIR 0x10u

/* Split "/home/notes.txt" -> dir="HOME", file="NOTES.TXT".
 * dir_out/file_out must hold 16 bytes. Depth capped at 1. */
static int split(const char *p, char *dir_out, char *file_out)
{
    int di = 0;
    int fi = 0;

    if (p[0] != '/') {
        return -1;
    }
    p++;
    while (*p != '\0' && *p != '/' && di < 12) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') {
            c -= (char)32;
        }
        dir_out[di++] = c;
    }
    dir_out[di] = '\0';
    if (*p == '/') {
        p++;
    }
    while (*p != '\0' && *p != '/' && fi < 12) {
        file_out[fi++] = *p++;
    }
    file_out[fi] = '\0';
    if (*p != '\0') {
        return -1; /* deeper than one level */
    }
    return 0;
}

static void emit_one(const char *name, uint8_t attr, uint32_t size)
{
    if (attr & ATTR_DIR) {
        k_color(11, 0); /* light cyan for dirs */
        k_print("  <DIR> ");
        k_print(name);
        k_print("\n");
        k_color(7, 0);
    } else {
        char tmp[12];
        int i = 0;
        uint32_t s = size;
        k_print("  file  ");
        k_print(name);
        k_print("  ");
        if (s == 0) {
            k_print("0B");
        } else {
            while (s > 0 && i < 11) {
                tmp[i++] = (char)('0' + (s % 10));
                s /= 10;
            }
            while (i > 0) {
                char c = tmp[--i];
                k_putc(c);
            }
            k_print("B");
        }
        k_print("\n");
    }
}

int vfs_mount(void)
{
    heap_init();
    if (fat_mount() != 0) {
        k_print("vfs: FAT mount failed (run make fs?)\n");
        return -1;
    }
    return 0;
}

int vfs_ls(const char *path)
{
    char dir[16];
    char file[16];

    if (path[0] == '\0') {
        /* bare `ls`: show the three system dirs. */
        k_print("  <DIR> BIN\n  <DIR> SYS\n  <DIR> HOME\n");
        return 0;
    }
    if (split(path, dir, file) != 0 || file[0] != '\0') {
        k_print("ls: usage: ls /bin | ls /home | ls /\n");
        return -1;
    }
    if (dir[0] == '\0') {
        k_print("  <DIR> BIN\n  <DIR> SYS\n  <DIR> HOME\n");
        return 0;
    }
    if (fat_list(dir, emit_one) != 0) {
        k_print("ls: no such dir\n");
        return -1;
    }
    return 0;
}

int vfs_cat(const char *path)
{
    char dir[16];
    char file[16];
    uint8_t *buf;
    uint32_t len;

    if (split(path, dir, file) != 0 || dir[0] == '\0' || file[0] == '\0') {
        k_print("cat: usage: cat /home/file.txt\n");
        return -1;
    }
    if (fat_read(dir, file, &buf, &len) != 0) {
        k_print("cat: no such file\n");
        return -1;
    }
    for (uint32_t i = 0; i < len; i++) {
        k_putc((char)buf[i]);
    }
    if (len == 0 || buf[len - 1] != '\n') {
        k_putc('\n');
    }
    kfree(buf);
    return 0;
}

int vfs_rm(const char *path)
{
    char dir[16];
    char file[16];

    if (split(path, dir, file) != 0 || dir[0] == '\0' || file[0] == '\0') {
        k_print("rm: usage: rm /home/file.txt\n");
        return -1;
    }
    if (fat_delete(dir, file) != 0) {
        k_print("rm: failed (missing? dir not empty?)\n");
        return -1;
    }
    return 0;
}

int vfs_mkdir(const char *path)
{
    char dir[16];
    char file[16];

    if (split(path, dir, file) != 0 || dir[0] == '\0' || file[0] != '\0') {
        k_print("mkdir: usage: mkdir /games (root only)\n");
        return -1;
    }
    if (fat_mkdir(dir) != 0) {
        k_print("mkdir: failed (exists? root full? disk full?)\n");
        return -1;
    }
    return 0;
}

int vfs_save(const char *path, const uint8_t *data, uint32_t len)
{
    char dir[16];
    char file[16];

    if (split(path, dir, file) != 0 || dir[0] == '\0' || file[0] == '\0') {
        return -1;
    }
    return fat_write(dir, file, data, len);
}
