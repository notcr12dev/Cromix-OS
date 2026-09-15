/* Cronix OS · fat16.c: BPB-driven FAT16, both FAT copies updated.
 * Cluster 0/1 reserved; chain end >= 0xFFF8; dir entry = 32 bytes. */
#include "fat.h"

#include "ata.h"
#include "heap.h"

struct __attribute__((packed)) dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt;
    uint8_t ctime_cs;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cl_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cl_lo;
    uint32_t size;
};

#define ATTR_DIR 0x10u
#define ATTR_VOL 0x08u

static uint16_t g_spc;
static uint16_t g_reserved;
static uint8_t g_fats;
static uint16_t g_root_entries;
static uint16_t g_fat_sectors;
static uint32_t g_clusters;
static uint32_t g_root_lba;
static uint32_t g_data_lba;

static uint8_t g_sec[512];

static uint16_t rd16(const uint8_t *b, uint32_t o)
{
    return (uint16_t)(b[o] | ((uint16_t)b[o + 1] << 8));
}

static uint32_t rd32(const uint8_t *b, uint32_t o)
{
    return (uint32_t)(b[o] | ((uint32_t)b[o + 1] << 8) |
                      ((uint32_t)b[o + 2] << 16) |
                      ((uint32_t)b[o + 3] << 24));
}

int fat_mount(void)
{
    uint32_t root_sec;
    uint32_t total;
    uint32_t data_sec;

    if (ata_read(FS_LBA, 1, g_sec) != 0) {
        return -1;
    }
    if (g_sec[510] != 0x55 || g_sec[511] != 0xAA) {
        return -1;
    }
    if (rd16(g_sec, 11) != 512 || g_sec[16] != 2) {
        return -1; /* we only speak 512B sectors, 2 FATs */
    }
    g_spc = g_sec[13];
    g_reserved = rd16(g_sec, 14);
    g_fats = g_sec[16];
    g_root_entries = rd16(g_sec, 17);
    g_fat_sectors = rd16(g_sec, 22);
    total = rd16(g_sec, 19);
    if (total == 0) {
        total = rd32(g_sec, 32);
    }
    if (g_spc == 0 || g_fat_sectors == 0) {
        return -1;
    }
    root_sec = ((uint32_t)g_root_entries * 32 + 511) / 512;
    g_root_lba = FS_LBA + g_reserved + (uint32_t)g_fats * g_fat_sectors;
    g_data_lba = g_root_lba + root_sec;
    data_sec = total - g_reserved - (uint32_t)g_fats * g_fat_sectors - root_sec;
    g_clusters = data_sec / g_spc;
    if (g_clusters < 4085 || g_clusters > 65525) {
        return -1; /* not FAT16 */
    }
    return 0;
}

static uint32_t cl_lba(uint32_t n)
{
    return g_data_lba + (n - 2) * g_spc;
}

static int fat_get(uint32_t n, uint16_t *out)
{
    uint32_t off = n * 2;
    uint32_t sec = FS_LBA + g_reserved + off / 512;
    uint32_t o = off % 512; /* even, so o+1 <= 511 always */

    if (ata_read(sec, 1, g_sec) != 0) {
        return -1;
    }
    *out = (uint16_t)(g_sec[o] | ((uint16_t)g_sec[o + 1] << 8));
    return 0;
}

static int fat_set(uint32_t n, uint16_t v)
{
    uint32_t off = n * 2;
    uint32_t o = off % 512;

    for (uint8_t f = 0; f < g_fats; f++) {
        uint32_t sec = FS_LBA + g_reserved + f * g_fat_sectors + off / 512;
        if (ata_read(sec, 1, g_sec) != 0) {
            return -1;
        }
        g_sec[o] = (uint8_t)(v & 0xFF);
        g_sec[o + 1] = (uint8_t)(v >> 8);
        if (ata_write(sec, 1, g_sec) != 0) {
            return -1;
        }
    }
    return 0;
}

/* First free cluster (>= 2), marked end-of-chain. */
static int32_t cl_alloc(void)
{
    for (uint32_t n = 2; n < 2 + g_clusters; n++) {
        uint16_t v;
        if (fat_get(n, &v) != 0) {
            return -1;
        }
        if (v == 0) {
            if (fat_set(n, 0xFFFF) != 0) {
                return -1;
            }
            return (int32_t)n;
        }
    }
    return -1; /* disk full */
}

static void cl_free_chain(uint32_t n)
{
    while (n >= 2 && n < 0xFFF0) {
        uint16_t nx;
        if (fat_get(n, &nx) != 0) {
            break;
        }
        fat_set(n, 0);
        if (nx >= 0xFFF8) {
            break;
        }
        n = nx;
    }
}

/* LBA of entry idx inside dir (dir_cl 0 = root). */
static int dir_lba(uint32_t dir_cl, uint32_t idx, uint32_t *lba)
{
    if (dir_cl == 0) {
        *lba = g_root_lba + idx / 16;
        return 0;
    }
    {
        uint32_t per = (uint32_t)g_spc * 16;
        uint32_t step = idx / per;
        uint32_t n = dir_cl;
        for (uint32_t s = 0; s < step; s++) {
            uint16_t nx;
            if (fat_get(n, &nx) != 0) {
                return -1;
            }
            if (nx < 2 || nx >= 0xFFF0) {
                return -1;
            }
            n = nx;
        }
        *lba = cl_lba(n) + (idx / 16) % g_spc;
        return 0;
    }
}

static int name_same(const uint8_t a[11], const uint8_t b[11])
{
    for (int i = 0; i < 11; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Find entry by 8.3 name. Fills entry + its sector/index. */
static int dir_find(uint32_t dir_cl, const uint8_t name[11],
                    struct dirent *e, uint32_t *e_lba, uint32_t *e_idx)
{
    uint32_t max = (dir_cl == 0)
        ? g_root_entries
        : g_clusters * (uint32_t)g_spc * 16;

    for (uint32_t idx = 0; idx < max; idx++) {
        uint32_t lba;
        struct dirent *d;

        if (dir_lba(dir_cl, idx, &lba) != 0) {
            return -1;
        }
        if (ata_read(lba, 1, g_sec) != 0) {
            return -1;
        }
        d = (struct dirent *)(g_sec + (idx % 16) * 32);
        if (d->name[0] == 0x00) {
            return -1; /* end of directory */
        }
        if (d->name[0] == 0xE5) {
            continue;
        }
        if (name_same(d->name, name)) {
            *e = *d;
            *e_lba = lba;
            *e_idx = idx;
            return 0;
        }
    }
    return -1;
}

/* Free slot for a new entry; grows subdirs by one cluster. */
static int dir_slot(uint32_t dir_cl, uint32_t *e_lba, uint32_t *e_idx)
{
    uint32_t max = (dir_cl == 0)
        ? g_root_entries
        : g_clusters * (uint32_t)g_spc * 16;

    for (uint32_t idx = 0; idx < max; idx++) {
        uint32_t lba;
        struct dirent *d;

        if (dir_lba(dir_cl, idx, &lba) != 0) {
            /* Chain end in a subdir: append one cluster. */
            int32_t nc;
            uint16_t last;
            uint32_t n;
            if (dir_cl == 0) {
                return -1; /* root is fixed size */
            }
            nc = cl_alloc();
            if (nc < 0) {
                return -1;
            }
            /* Link after the last cluster of this dir. */
            n = dir_cl;
            for (;;) {
                if (fat_get(n, &last) != 0) {
                    return -1;
                }
                if (last >= 0xFFF8) {
                    break;
                }
                n = last;
            }
            if (fat_set(n, (uint16_t)nc) != 0) {
                return -1;
            }
            for (uint8_t s = 0; s < g_spc; s++) {
                for (int i = 0; i < 512; i++) {
                    g_sec[i] = 0;
                }
                if (ata_write(cl_lba((uint32_t)nc) + s, 1, g_sec) != 0) {
                    return -1;
                }
            }
            if (dir_lba(dir_cl, idx, &lba) != 0) {
                return -1;
            }
        }
        if (ata_read(lba, 1, g_sec) != 0) {
            return -1;
        }
        d = (struct dirent *)(g_sec + (idx % 16) * 32);
        if (d->name[0] == 0x00 || d->name[0] == 0xE5) {
            *e_lba = lba;
            *e_idx = idx;
            return 0;
        }
    }
    return -1;
}

static void to83(const char *name, uint8_t out[11])
{
    int i = 0;
    int j;

    for (int k = 0; k < 11; k++) {
        out[k] = ' ';
    }
    while (*name != '\0' && *name != '.' && i < 8) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') {
            c -= (char)32;
        }
        out[i++] = (uint8_t)c;
    }
    if (*name == '.') {
        name++;
    }
    j = 8;
    while (*name != '\0' && j < 11) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') {
            c -= (char)32;
        }
        out[j++] = (uint8_t)c;
    }
}

/* Resolve "BIN"/"SYS"/"HOME" (or "" for root) to a dir cluster. */
static int resolve_dir(const char *dir, uint32_t *cl)
{
    uint8_t n83[11];
    struct dirent e;
    uint32_t lba;
    uint32_t idx;

    if (dir[0] == '\0') {
        *cl = 0;
        return 0;
    }
    to83(dir, n83);
    if (dir_find(0, n83, &e, &lba, &idx) != 0) {
        return -1;
    }
    if (!(e.attr & ATTR_DIR)) {
        return -1;
    }
    *cl = e.cl_lo; /* FAT16: high word always 0 */
    return 0;
}

int fat_list(const char *dir,
             void (*emit)(const char *name, uint8_t attr, uint32_t size))
{
    uint32_t dcl;
    uint32_t max;
    char pretty[13];

    if (resolve_dir(dir, &dcl) != 0) {
        return -1;
    }
    max = (dcl == 0) ? g_root_entries : g_clusters * (uint32_t)g_spc * 16;
    for (uint32_t idx = 0; idx < max; idx++) {
        uint32_t lba;
        struct dirent *d;
        int p;

        if (dir_lba(dcl, idx, &lba) != 0) {
            return 0; /* subdir chain end */
        }
        if (ata_read(lba, 1, g_sec) != 0) {
            return -1;
        }
        d = (struct dirent *)(g_sec + (idx % 16) * 32);
        if (d->name[0] == 0x00) {
            return 0;
        }
        if (d->name[0] == 0xE5 || (d->attr & ATTR_VOL)) {
            continue;
        }
        /* "NAME    .EXT" -> "NAME.EXT" */
        p = 0;
        for (int i = 0; i < 8 && d->name[i] != ' '; i++) {
            pretty[p++] = (char)d->name[i];
        }
        if (d->name[8] != ' ') {
            pretty[p++] = '.';
            for (int i = 8; i < 11 && d->name[i] != ' '; i++) {
                pretty[p++] = (char)d->name[i];
            }
        }
        pretty[p] = '\0';
        emit(pretty, d->attr, d->size);
    }
    return 0;
}

int fat_read(const char *dir, const char *name,
             uint8_t **out, uint32_t *len)
{
    uint32_t dcl;
    uint8_t n83[11];
    struct dirent e;
    uint32_t lba;
    uint32_t idx;
    uint8_t *buf;
    uint32_t done = 0;
    uint32_t n;

    if (resolve_dir(dir, &dcl) != 0) {
        return -1;
    }
    to83(name, n83);
    if (dir_find(dcl, n83, &e, &lba, &idx) != 0) {
        return -1;
    }
    if (e.attr & ATTR_DIR) {
        return -1;
    }
    buf = kmalloc(e.size != 0 ? e.size : 1);
    if (buf == (uint8_t *)0) {
        return -1;
    }
    n = e.cl_lo;
    while (done < e.size) {
        if (n < 2 || n >= 0xFFF0) {
            kfree(buf);
            return -1; /* broken chain */
        }
        for (uint8_t s = 0; s < g_spc && done < e.size; s++) {
            uint32_t want = e.size - done;
            if (want > 512) {
                want = 512;
            }
            if (ata_read(cl_lba(n) + s, 1, g_sec) != 0) {
                kfree(buf);
                return -1;
            }
            for (uint32_t i = 0; i < want; i++) {
                buf[done++] = g_sec[i];
            }
        }
        {
            uint16_t nx;
            if (fat_get(n, &nx) != 0) {
                kfree(buf);
                return -1;
            }
            if (done < e.size && nx >= 0xFFF8) {
                kfree(buf);
                return -1; /* file shorter than its size */
            }
            n = nx;
        }
    }
    *out = buf;
    *len = e.size;
    return 0;
}

/* Write (or rewrite) one sector of g_sec helpers. */
static void zero_sec(void)
{
    for (int i = 0; i < 512; i++) {
        g_sec[i] = 0;
    }
}

int fat_write(const char *dir, const char *name,
              const uint8_t *data, uint32_t len)
{
    uint32_t dcl;
    uint8_t n83[11];
    struct dirent e;
    uint32_t e_lba;
    uint32_t e_idx;
    int exists;
    uint32_t per = (uint32_t)g_spc * 512;
    uint32_t need = (len + per - 1) / per;
    uint32_t first = 0;
    uint32_t prev = 0;
    uint32_t done = 0;

    if (resolve_dir(dir, &dcl) != 0) {
        return -1;
    }
    to83(name, n83);
    exists = (dir_find(dcl, n83, &e, &e_lba, &e_idx) == 0);
    if (exists) {
        if (e.attr & ATTR_DIR) {
            return -1;
        }
        cl_free_chain(e.cl_lo); /* old data goes first */
    } else {
        if (dir_slot(dcl, &e_lba, &e_idx) != 0) {
            return -1;
        }
        for (int i = 0; i < 11; i++) {
            e.name[i] = n83[i];
        }
        e.attr = 0;
    }
    /* Allocate the new chain. */
    for (uint32_t c = 0; c < need; c++) {
        int32_t nc = cl_alloc();
        if (nc < 0) {
            if (first != 0) {
                cl_free_chain(first);
            }
            return -1;
        }
        if (first == 0) {
            first = (uint32_t)nc;
        } else {
            fat_set(prev, (uint16_t)nc);
        }
        prev = (uint32_t)nc;
    }
    /* Pour data, zero-pad the tail. */
    {
        uint32_t n = first;
        while (done < len) {
            for (uint8_t s = 0; s < g_spc && done < len; s++) {
                for (uint32_t i = 0; i < 512; i++) {
                    g_sec[i] = (done < len) ? data[done++] : 0;
                }
                if (ata_write(cl_lba(n) + s, 1, g_sec) != 0) {
                    cl_free_chain(first);
                    return -1;
                }
            }
            {
                uint16_t nx;
                fat_get(n, &nx);
                n = nx;
            }
        }
    }
    /* Commit the directory entry. */
    if (ata_read(e_lba, 1, g_sec) != 0) {
        cl_free_chain(first);
        return -1;
    }
    {
        struct dirent *d = (struct dirent *)(g_sec + (e_idx % 16) * 32);
        for (int i = 0; i < 11; i++) {
            d->name[i] = e.name[i];
        }
        d->attr = 0;
        d->cl_lo = (uint16_t)first; /* 0 when len == 0: empty file */
        d->cl_hi = 0;
        d->size = len;
        if (ata_write(e_lba, 1, g_sec) != 0) {
            return -1;
        }
    }
    return 0;
}

int fat_delete(const char *dir, const char *name)
{
    uint32_t dcl;
    uint8_t n83[11];
    struct dirent e;
    uint32_t e_lba;
    uint32_t e_idx;

    if (resolve_dir(dir, &dcl) != 0) {
        return -1;
    }
    to83(name, n83);
    if (dir_find(dcl, n83, &e, &e_lba, &e_idx) != 0) {
        return -1;
    }
    if (e.attr & ATTR_DIR) {
        /* Refuse non-empty dirs: any live entry beyond . and .. */
        uint32_t sub = e.cl_lo;
        uint32_t max = g_clusters * (uint32_t)g_spc * 16;
        for (uint32_t idx = 2; idx < max; idx++) {
            uint32_t lba;
            struct dirent *d;
            if (dir_lba(sub, idx, &lba) != 0) {
                break;
            }
            if (ata_read(lba, 1, g_sec) != 0) {
                return -1;
            }
            d = (struct dirent *)(g_sec + (idx % 16) * 32);
            if (d->name[0] == 0x00) {
                break;
            }
            if (d->name[0] != 0xE5) {
                return -1;
            }
        }
    }
    cl_free_chain(e.cl_lo);
    if (ata_read(e_lba, 1, g_sec) != 0) {
        return -1;
    }
    ((struct dirent *)(g_sec + (e_idx % 16) * 32))->name[0] = 0xE5;
    if (ata_write(e_lba, 1, g_sec) != 0) {
        return -1;
    }
    return 0;
}

static void dot_entry(struct dirent *d, const char *dot, uint32_t cl)
{
    for (int i = 0; i < 11; i++) {
        d->name[i] = ' ';
    }
    d->name[0] = (uint8_t)dot[0];
    if (dot[1] == '.') {
        d->name[1] = '.';
    }
    d->attr = ATTR_DIR;
    d->cl_lo = (uint16_t)cl;
    d->cl_hi = 0;
    d->size = 0;
}

int fat_mkdir(const char *name)
{
    uint8_t n83[11];
    struct dirent e;
    uint32_t e_lba;
    uint32_t e_idx;
    int32_t nc;

    to83(name, n83);
    if (dir_find(0, n83, &e, &e_lba, &e_idx) == 0) {
        return -1; /* already exists */
    }
    if (dir_slot(0, &e_lba, &e_idx) != 0) {
        return -1;
    }
    nc = cl_alloc();
    if (nc < 0) {
        return -1;
    }
    zero_sec();
    dot_entry((struct dirent *)g_sec, ".", (uint32_t)nc);
    dot_entry((struct dirent *)(g_sec + 32), "..", 0);
    for (uint8_t s = 0; s < g_spc; s++) {
        if (s == 0) {
            /* first sector already holds . and .. */
        } else {
            zero_sec();
        }
        if (ata_write(cl_lba((uint32_t)nc) + s, 1, g_sec) != 0) {
            cl_free_chain((uint32_t)nc);
            return -1;
        }
    }
    if (ata_read(e_lba, 1, g_sec) != 0) {
        cl_free_chain((uint32_t)nc);
        return -1;
    }
    {
        struct dirent *d = (struct dirent *)(g_sec + (e_idx % 16) * 32);
        for (int i = 0; i < 11; i++) {
            d->name[i] = n83[i];
        }
        d->attr = ATTR_DIR;
        d->cl_lo = (uint16_t)nc;
        d->cl_hi = 0;
        d->size = 0;
        if (ata_write(e_lba, 1, g_sec) != 0) {
            return -1;
        }
    }
    return 0;
}
