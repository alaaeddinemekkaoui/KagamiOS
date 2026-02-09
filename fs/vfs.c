#include "vfs.h"

static Ext4Fs *g_ext4 = 0;

int vfs_mount_ext4(Ext4Fs *fs) {
    g_ext4 = fs;
    return g_ext4 != 0;
}

int vfs_is_mounted(void) {
    return g_ext4 != 0;
}

int vfs_list_dir(const char *path, char *out, uint32_t max_size) {
    if (!g_ext4) {
        return 0;
    }
    return ext4_list_dir(g_ext4, path, out, max_size);
}

int vfs_read_file(const char *path, void *buffer, uint32_t max_size, uint32_t *out_size) {
    if (!g_ext4) {
        return 0;
    }
    return ext4_read_file(g_ext4, path, buffer, max_size, out_size);
}

int vfs_write_file(const char *path, const void *buffer, uint32_t size) {
    if (!g_ext4) {
        return 0;
    }
    return ext4_write_file(g_ext4, path, buffer, size);
}
