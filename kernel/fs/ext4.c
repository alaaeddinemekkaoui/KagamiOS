#include "ext4.h"
#include "../include/serial.h"

#define EXT4_SUPERBLOCK_OFFSET 1024

#pragma pack(push, 1)
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
} Ext4SuperblockRaw;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
} Ext4GroupDesc;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint8_t  i_block[60];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
} Ext4Inode;

typedef struct {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} Ext4ExtentHeader;

typedef struct {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} Ext4Extent;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[255];
} Ext4DirEntry;
#pragma pack(pop)

#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_FT_DIR 2
#define EXT4_FT_REG_FILE 1

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char *a, const char *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int ext4_read_block(Ext4Fs *fs, uint64_t block, void *buffer) {
    uint32_t sectors = fs->sb.block_size / BLOCK_SECTOR_SIZE;
    uint64_t lba = fs->partition_lba + block * sectors;
    return fs->device->read(fs->device, lba, sectors, buffer);
}

static int ext4_read_inode(Ext4Fs *fs, uint32_t inode_num, Ext4Inode *out_inode) {
    uint32_t inode_index = inode_num - 1;
    uint32_t group = inode_index / fs->sb.inodes_per_group;
    uint32_t index_in_group = inode_index % fs->sb.inodes_per_group;

    uint32_t block_size = fs->sb.block_size;
    uint32_t group_desc_block = (block_size == 1024) ? 2 : 1;
    uint32_t group_desc_offset = group * fs->sb.group_desc_size;
    uint32_t gd_block = group_desc_block + (group_desc_offset / block_size);
    uint32_t gd_offset_in_block = group_desc_offset % block_size;

    uint8_t gd_buf[4096];
    if (!ext4_read_block(fs, gd_block, gd_buf)) {
        return 0;
    }

    Ext4GroupDesc *gd = (Ext4GroupDesc *)(gd_buf + gd_offset_in_block);
    uint32_t inode_table_block = gd->bg_inode_table_lo;

    uint32_t inode_size = fs->sb.inode_size;
    uint32_t inode_offset = index_in_group * inode_size;
    uint32_t inode_block = inode_table_block + (inode_offset / block_size);
    uint32_t inode_offset_in_block = inode_offset % block_size;

    uint8_t inode_buf[4096];
    if (!ext4_read_block(fs, inode_block, inode_buf)) {
        return 0;
    }

    Ext4Inode *inode = (Ext4Inode *)(inode_buf + inode_offset_in_block);
    *out_inode = *inode;
    return 1;
}

static uint64_t extent_start_block(const Ext4Extent *ext) {
    return ((uint64_t)ext->ee_start_hi << 32) | ext->ee_start_lo;
}

static int ext4_read_extent_blocks(Ext4Fs *fs, Ext4Inode *inode, uint64_t offset, void *buffer, uint32_t size, uint32_t *out_read) {
    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)inode->i_block;
    if (hdr->eh_magic != 0xF30A || hdr->eh_depth != 0) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    uint32_t remaining = size;
    uint32_t total_read = 0;

    Ext4Extent *ext = (Ext4Extent *)(hdr + 1);
    for (uint16_t i = 0; i < hdr->eh_entries && remaining > 0; i++) {
        uint64_t start_block = extent_start_block(&ext[i]);
        uint32_t block_count = ext[i].ee_len & 0x7FFF;

        for (uint32_t b = 0; b < block_count && remaining > 0; b++) {
            uint64_t file_block = ext[i].ee_block + b;
            uint64_t byte_start = file_block * block_size;
            if (byte_start + block_size <= offset) {
                continue;
            }

            uint8_t block_buf[4096];
            if (!ext4_read_block(fs, start_block + b, block_buf)) {
                return 0;
            }

            uint32_t copy_start = 0;
            if (offset > byte_start) {
                copy_start = (uint32_t)(offset - byte_start);
            }
            uint32_t copy_len = block_size - copy_start;
            if (copy_len > remaining) {
                copy_len = remaining;
            }

            uint8_t *dst = (uint8_t *)buffer + total_read;
            for (uint32_t k = 0; k < copy_len; k++) {
                dst[k] = block_buf[copy_start + k];
            }

            remaining -= copy_len;
            total_read += copy_len;
        }
    }

    if (out_read) {
        *out_read = total_read;
    }
    return 1;
}

static int ext4_find_in_dir(Ext4Fs *fs, Ext4Inode *dir_inode, const char *name, uint32_t name_len, uint32_t *out_inode, uint8_t *out_type) {
    if (!(dir_inode->i_flags & EXT4_EXTENTS_FL)) {
        return 0;
    }

    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)dir_inode->i_block;
    if (hdr->eh_magic != 0xF30A || hdr->eh_depth != 0) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    Ext4Extent *ext = (Ext4Extent *)(hdr + 1);

    for (uint16_t i = 0; i < hdr->eh_entries; i++) {
        uint64_t start_block = extent_start_block(&ext[i]);
        uint32_t block_count = ext[i].ee_len & 0x7FFF;

        for (uint32_t b = 0; b < block_count; b++) {
            uint8_t block_buf[4096];
            if (!ext4_read_block(fs, start_block + b, block_buf)) {
                return 0;
            }

            uint32_t offset = 0;
            while (offset + 8 < block_size) {
                Ext4DirEntry *entry = (Ext4DirEntry *)(block_buf + offset);
                if (entry->rec_len == 0) {
                    break;
                }

                if (entry->inode != 0 && entry->name_len == name_len && str_eq(entry->name, name, name_len)) {
                    *out_inode = entry->inode;
                    if (out_type) {
                        *out_type = entry->file_type;
                    }
                    return 1;
                }

                offset += entry->rec_len;
            }
        }
    }

    return 0;
}

static int read_superblock(BlockDevice *dev, uint64_t partition_lba, Ext4SuperblockInfo *out_sb) {
    uint8_t buf[BLOCK_SECTOR_SIZE * 2];

    if (!dev || !dev->read) {
        return 0;
    }

    if (!dev->read(dev, partition_lba + EXT4_SUPERBLOCK_LBA, 2, buf)) {
        return 0;
    }

    Ext4SuperblockRaw *raw = (Ext4SuperblockRaw *)buf;
    if (raw->s_magic != EXT4_SUPER_MAGIC) {
        return 0;
    }

    uint32_t block_size = 1024U << raw->s_log_block_size;

    out_sb->block_size = block_size;
    out_sb->inodes_per_group = raw->s_inodes_per_group;
    out_sb->inode_size = raw->s_inode_size;
    out_sb->blocks_per_group = raw->s_blocks_per_group;
    out_sb->total_blocks = raw->s_blocks_count_lo;
    out_sb->total_inodes = raw->s_inodes_count;
    out_sb->features_compat = raw->s_feature_compat;
    out_sb->features_incompat = raw->s_feature_incompat;
    out_sb->features_ro_compat = raw->s_feature_ro_compat;
    out_sb->first_data_block = raw->s_first_data_block;
    out_sb->group_desc_size = 32;

    return 1;
}

int ext4_mount(Ext4Fs *fs, BlockDevice *dev, uint64_t partition_lba) {
    if (!fs || !dev) {
        return 0;
    }

    if (!read_superblock(dev, partition_lba, &fs->sb)) {
        serial_write("EXT4: invalid superblock\n");
        return 0;
    }

    fs->device = dev;
    fs->partition_lba = partition_lba;

    serial_write("EXT4: superblock loaded\n");
    return 1;
}

int ext4_read_file(Ext4Fs *fs, const char *path, void *buffer, uint32_t max_size, uint32_t *out_size) {
    if (!fs || !path || path[0] != '/') {
        return 0;
    }

    Ext4Inode inode;
    if (!ext4_read_inode(fs, 2, &inode)) {
        return 0;
    }

    const char *p = path;
    while (*p == '/') {
        p++;
    }

    uint32_t current_inode_num = 2;
    uint8_t current_type = EXT4_FT_DIR;

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        uint32_t name_len = (uint32_t)(p - start);

        if (current_type != EXT4_FT_DIR) {
            return 0;
        }

        Ext4Inode dir_inode;
        if (!ext4_read_inode(fs, current_inode_num, &dir_inode)) {
            return 0;
        }

        uint32_t next_inode_num = 0;
        uint8_t next_type = 0;
        if (!ext4_find_in_dir(fs, &dir_inode, start, name_len, &next_inode_num, &next_type)) {
            return 0;
        }

        current_inode_num = next_inode_num;
        current_type = next_type;

        while (*p == '/') {
            p++;
        }
    }

    Ext4Inode file_inode;
    if (!ext4_read_inode(fs, current_inode_num, &file_inode)) {
        return 0;
    }

    if (!(file_inode.i_flags & EXT4_EXTENTS_FL)) {
        return 0;
    }

    uint64_t size = ((uint64_t)file_inode.i_size_high << 32) | file_inode.i_size_lo;
    if (size > max_size) {
        size = max_size;
    }

    uint32_t read_size = 0;
    if (!ext4_read_extent_blocks(fs, &file_inode, 0, buffer, (uint32_t)size, &read_size)) {
        return 0;
    }

    if (out_size) {
        *out_size = read_size;
    }

    return 1;
}
