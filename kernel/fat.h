#ifndef FAT_H
#define FAT_H

/* Cronix OS · FAT16 read/write over ATA. 8.3 uppercase names.
 * One level below /bin /sys /home (VFS enforces depth).
 * FS lives at fixed FS_LBA (see scripts/mkfs.py + mkimage.py).
 * All functions return 0 on OK, -1 on error. */
#include <stdint.h>

#define FS_LBA 2048

int fat_mount(void);

/* dir: "BIN", "SYS", "HOME" (already uppercased by VFS). */
int fat_list(const char *dir,
             void (*emit)(const char *name, uint8_t attr, uint32_t size));
int fat_read(const char *dir, const char *name,
             uint8_t **out, uint32_t *len); /* *out via kmalloc, caller frees */
int fat_write(const char *dir, const char *name,
              const uint8_t *data, uint32_t len); /* create or replace */
int fat_delete(const char *dir, const char *name);
int fat_mkdir(const char *name); /* new dir at volume root */

#endif /* FAT_H */
