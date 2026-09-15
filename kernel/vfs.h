#ifndef VFS_H
#define VFS_H

/* Cronix OS · tiny VFS: paths like /home/notes.txt, depth 1.
 * System dirs (created by mkfs): /bin /sys /home.
 * No users yet: everything runs as one owner. */
#include <stdint.h>

int vfs_mount(void); /* heap + FAT; 0 on OK */
int vfs_ls(const char *path);    /* ls /bin · ls /home · ls / */
int vfs_cat(const char *path);   /* print file to VGA+serial */
int vfs_rm(const char *path);
int vfs_mkdir(const char *path); /* mkdir /games (root only) */
int vfs_save(const char *path, const uint8_t *data, uint32_t len);

#endif /* VFS_H */
