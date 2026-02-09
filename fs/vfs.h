#ifndef KAGAMI_VFS_H
#define KAGAMI_VFS_H

#include "types.h"
#include "fs/ext4/ext4.h"

int vfs_mount_ext4(Ext4Fs *fs);
int vfs_is_mounted(void);

int vfs_list_dir(const char *path, char *out, uint32_t max_size);
int vfs_read_file(const char *path, void *buffer, uint32_t max_size, uint32_t *out_size);
int vfs_write_file(const char *path, const void *buffer, uint32_t size);

#endif
