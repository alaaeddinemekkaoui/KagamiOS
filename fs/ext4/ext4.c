#include "ext4.h"
#include "serial.h"
#include "klog.h"

#define EXT4_SUPERBLOCK_OFFSET 1024
#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_FT_DIR 2
#define EXT4_FT_REG_FILE 1
#define EXT4_EXTENT_HEADER_MAGIC 0xF30A

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

static uint32_t align4(uint32_t v) {
    return (v + 3) & ~3U;
}

static int ext4_read_block(Ext4Fs *fs, uint64_t block, void *buffer) {
    uint32_t sectors = fs->sb.block_size / BLOCK_SECTOR_SIZE;
    uint64_t lba = fs->partition_lba + block * sectors;
    return fs->device->read(fs->device, lba, sectors, buffer);
}

static int ext4_write_block(Ext4Fs *fs, uint64_t block, const void *buffer) {
    if (!fs->device->write) {
        return 0;
    }
    uint32_t sectors = fs->sb.block_size / BLOCK_SECTOR_SIZE;
    uint64_t lba = fs->partition_lba + block * sectors;
    return fs->device->write(fs->device, lba, sectors, buffer);
}

static int ext4_read_super_raw(Ext4Fs *fs, Ext4SuperblockRaw *raw) {
    uint8_t buf[BLOCK_SECTOR_SIZE * 2];
    if (!fs->device->read(fs->device, fs->partition_lba + EXT4_SUPERBLOCK_LBA, 2, buf)) {
        return 0;
    }
    Ext4SuperblockRaw *sb = (Ext4SuperblockRaw *)buf;
    *raw = *sb;
    return 1;
}

static int ext4_write_super_raw(Ext4Fs *fs, Ext4SuperblockRaw *raw) {
    uint8_t buf[BLOCK_SECTOR_SIZE * 2];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0;
    }
    Ext4SuperblockRaw *sb = (Ext4SuperblockRaw *)buf;
    *sb = *raw;
    return fs->device->write && fs->device->write(fs->device, fs->partition_lba + EXT4_SUPERBLOCK_LBA, 2, buf);
}

static int ext4_read_group_desc(Ext4Fs *fs, uint32_t group, Ext4GroupDesc *out, uint8_t *block_buf) {
    uint32_t block_size = fs->sb.block_size;
    uint32_t group_desc_block = (block_size == 1024) ? 2 : 1;
    uint32_t group_desc_offset = group * fs->sb.group_desc_size;
    uint32_t gd_block = group_desc_block + (group_desc_offset / block_size);
    uint32_t gd_offset_in_block = group_desc_offset % block_size;

    if (!ext4_read_block(fs, gd_block, block_buf)) {
        return 0;
    }

    Ext4GroupDesc *gd = (Ext4GroupDesc *)(block_buf + gd_offset_in_block);
    *out = *gd;
    return 1;
}

static int ext4_write_group_desc(Ext4Fs *fs, uint32_t group, Ext4GroupDesc *gd, uint8_t *block_buf) {
    uint32_t block_size = fs->sb.block_size;
    uint32_t group_desc_block = (block_size == 1024) ? 2 : 1;
    uint32_t group_desc_offset = group * fs->sb.group_desc_size;
    uint32_t gd_block = group_desc_block + (group_desc_offset / block_size);
    uint32_t gd_offset_in_block = group_desc_offset % block_size;

    Ext4GroupDesc *slot = (Ext4GroupDesc *)(block_buf + gd_offset_in_block);
    *slot = *gd;
    return ext4_write_block(fs, gd_block, block_buf);
}

static int ext4_read_inode(Ext4Fs *fs, uint32_t inode_num, Ext4Inode *out_inode, Ext4GroupDesc *out_gd) {
    uint32_t inode_index = inode_num - 1;
    uint32_t group = inode_index / fs->sb.inodes_per_group;
    uint32_t index_in_group = inode_index % fs->sb.inodes_per_group;

    uint32_t block_size = fs->sb.block_size;
    uint8_t gd_buf[4096];
    Ext4GroupDesc gd;
    if (!ext4_read_group_desc(fs, group, &gd, gd_buf)) {
        return 0;
    }

    uint32_t inode_table_block = gd.bg_inode_table_lo;
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
    if (out_gd) {
        *out_gd = gd;
    }
    return 1;
}

static int ext4_write_inode(Ext4Fs *fs, uint32_t inode_num, Ext4Inode *inode) {
    uint32_t inode_index = inode_num - 1;
    uint32_t group = inode_index / fs->sb.inodes_per_group;
    uint32_t index_in_group = inode_index % fs->sb.inodes_per_group;

    uint32_t block_size = fs->sb.block_size;
    uint8_t gd_buf[4096];
    Ext4GroupDesc gd;
    if (!ext4_read_group_desc(fs, group, &gd, gd_buf)) {
        return 0;
    }

    uint32_t inode_table_block = gd.bg_inode_table_lo;
    uint32_t inode_size = fs->sb.inode_size;
    uint32_t inode_offset = index_in_group * inode_size;
    uint32_t inode_block = inode_table_block + (inode_offset / block_size);
    uint32_t inode_offset_in_block = inode_offset % block_size;

    uint8_t inode_buf[4096];
    if (!ext4_read_block(fs, inode_block, inode_buf)) {
        return 0;
    }

    Ext4Inode *slot = (Ext4Inode *)(inode_buf + inode_offset_in_block);
    *slot = *inode;

    return ext4_write_block(fs, inode_block, inode_buf);
}

static uint64_t extent_start_block(const Ext4Extent *ext) {
    return ((uint64_t)ext->ee_start_hi << 32) | ext->ee_start_lo;
}

static int ext4_read_extent_blocks(Ext4Fs *fs, Ext4Inode *inode, uint64_t offset, void *buffer, uint32_t size, uint32_t *out_read) {
    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)inode->i_block;
    if (hdr->eh_magic != EXT4_EXTENT_HEADER_MAGIC || hdr->eh_depth != 0) {
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

static int ext4_write_extent_blocks(Ext4Fs *fs, Ext4Inode *inode, uint64_t offset, const void *buffer, uint32_t size) {
    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)inode->i_block;
    if (hdr->eh_magic != EXT4_EXTENT_HEADER_MAGIC || hdr->eh_depth != 0) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    uint32_t remaining = size;
    uint32_t total_written = 0;

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

            const uint8_t *src = (const uint8_t *)buffer + total_written;
            for (uint32_t k = 0; k < copy_len; k++) {
                block_buf[copy_start + k] = src[k];
            }

            if (!ext4_write_block(fs, start_block + b, block_buf)) {
                return 0;
            }

            remaining -= copy_len;
            total_written += copy_len;
        }
    }

    return remaining == 0;
}

static int ext4_find_in_dir(Ext4Fs *fs, Ext4Inode *dir_inode, const char *name, uint32_t name_len, uint32_t *out_inode, uint8_t *out_type) {
    if (!(dir_inode->i_flags & EXT4_EXTENTS_FL)) {
        return 0;
    }

    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)dir_inode->i_block;
    if (hdr->eh_magic != EXT4_EXTENT_HEADER_MAGIC || hdr->eh_depth != 0) {
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

static int ext4_resolve_path(Ext4Fs *fs, const char *path, uint32_t *out_inode, uint8_t *out_type) {
    if (!fs || !path || path[0] != '/') {
        return 0;
    }

    uint32_t current_inode_num = 2;
    uint8_t current_type = EXT4_FT_DIR;

    const char *p = path;
    while (*p == '/') {
        p++;
    }

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        uint32_t name_len = (uint32_t)(p - start);

        Ext4Inode dir_inode;
        if (!ext4_read_inode(fs, current_inode_num, &dir_inode, 0)) {
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

    if (out_inode) {
        *out_inode = current_inode_num;
    }
    if (out_type) {
        *out_type = current_type;
    }
    return 1;
}

static int ext4_alloc_block_run(Ext4Fs *fs, uint32_t count, uint32_t *out_block) {
    uint8_t gd_buf[4096];
    Ext4GroupDesc gd;
    if (!ext4_read_group_desc(fs, 0, &gd, gd_buf)) {
        return 0;
    }

    uint32_t bitmap_block = gd.bg_block_bitmap_lo;
    uint8_t bitmap[4096];
    if (!ext4_read_block(fs, bitmap_block, bitmap)) {
        return 0;
    }

    uint32_t total_blocks = fs->sb.blocks_per_group;
    uint32_t run = 0;
    uint32_t start = 0;

    for (uint32_t i = 0; i < total_blocks; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        int used = (bitmap[byte] >> bit) & 1;
        if (!used) {
            if (run == 0) {
                start = i;
            }
            run++;
            if (run == count) {
                for (uint32_t j = 0; j < count; j++) {
                    uint32_t idx = start + j;
                    bitmap[idx / 8] |= (1 << (idx % 8));
                }
                if (!ext4_write_block(fs, bitmap_block, bitmap)) {
                    return 0;
                }
                if (gd.bg_free_blocks_count_lo >= count) {
                    gd.bg_free_blocks_count_lo -= count;
                }
                ext4_write_group_desc(fs, 0, &gd, gd_buf);

                Ext4SuperblockRaw sbr;
                if (ext4_read_super_raw(fs, &sbr)) {
                    if (sbr.s_free_blocks_count_lo >= count) {
                        sbr.s_free_blocks_count_lo -= count;
                    }
                    ext4_write_super_raw(fs, &sbr);
                }

                *out_block = start + fs->sb.first_data_block;
                return 1;
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

static int ext4_alloc_inode(Ext4Fs *fs, uint32_t *out_inode) {
    uint8_t gd_buf[4096];
    Ext4GroupDesc gd;
    if (!ext4_read_group_desc(fs, 0, &gd, gd_buf)) {
        return 0;
    }

    uint32_t bitmap_block = gd.bg_inode_bitmap_lo;
    uint8_t bitmap[4096];
    if (!ext4_read_block(fs, bitmap_block, bitmap)) {
        return 0;
    }

    uint32_t total_inodes = fs->sb.inodes_per_group;
    for (uint32_t i = 0; i < total_inodes; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        int used = (bitmap[byte] >> bit) & 1;
        if (!used) {
            bitmap[byte] |= (1 << bit);
            if (!ext4_write_block(fs, bitmap_block, bitmap)) {
                return 0;
            }
            if (gd.bg_free_inodes_count_lo > 0) {
                gd.bg_free_inodes_count_lo -= 1;
            }
            ext4_write_group_desc(fs, 0, &gd, gd_buf);

            Ext4SuperblockRaw sbr;
            if (ext4_read_super_raw(fs, &sbr)) {
                if (sbr.s_free_inodes_count > 0) {
                    sbr.s_free_inodes_count -= 1;
                }
                ext4_write_super_raw(fs, &sbr);
            }

            *out_inode = i + 1;
            return 1;
        }
    }

    return 0;
}

static int ext4_add_dir_entry(Ext4Fs *fs, uint32_t dir_inode_num, const char *name, uint8_t file_type, uint32_t inode_num) {
    Ext4Inode dir_inode;
    if (!ext4_read_inode(fs, dir_inode_num, &dir_inode, 0)) {
        return 0;
    }

    if (!(dir_inode.i_flags & EXT4_EXTENTS_FL)) {
        return 0;
    }

    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)dir_inode.i_block;
    if (hdr->eh_magic != EXT4_EXTENT_HEADER_MAGIC || hdr->eh_depth != 0) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    uint32_t name_len = str_len(name);
    uint32_t entry_size = align4(8 + name_len);

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

                uint32_t used = align4(8 + entry->name_len);
                if (entry->rec_len >= used + entry_size) {
                    uint32_t new_offset = offset + used;
                    Ext4DirEntry *new_entry = (Ext4DirEntry *)(block_buf + new_offset);
                    new_entry->inode = inode_num;
                    new_entry->rec_len = entry->rec_len - used;
                    new_entry->name_len = (uint8_t)name_len;
                    new_entry->file_type = file_type;
                    for (uint32_t n = 0; n < name_len; n++) {
                        new_entry->name[n] = name[n];
                    }

                    entry->rec_len = used;

                    if (!ext4_write_block(fs, start_block + b, block_buf)) {
                        return 0;
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
        KERR("EXT4: invalid superblock");
        return 0;
    }

    fs->device = dev;
    fs->partition_lba = partition_lba;

    serial_write("EXT4: superblock loaded\n");
    KLOG("EXT4: superblock loaded");
    return 1;
}

int ext4_read_file(Ext4Fs *fs, const char *path, void *buffer, uint32_t max_size, uint32_t *out_size) {
    if (!fs || !path || path[0] != '/') {
        return 0;
    }

    uint32_t inode_num = 0;
    uint8_t type = 0;
    if (!ext4_resolve_path(fs, path, &inode_num, &type) || type != EXT4_FT_REG_FILE) {
        return 0;
    }

    Ext4Inode file_inode;
    if (!ext4_read_inode(fs, inode_num, &file_inode, 0)) {
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

int ext4_list_dir(Ext4Fs *fs, const char *path, char *out, uint32_t max_size) {
    if (!fs || !out || max_size == 0) {
        return 0;
    }

    uint32_t inode_num = 0;
    uint8_t type = 0;
    if (!ext4_resolve_path(fs, path, &inode_num, &type) || type != EXT4_FT_DIR) {
        return 0;
    }

    Ext4Inode dir_inode;
    if (!ext4_read_inode(fs, inode_num, &dir_inode, 0)) {
        return 0;
    }

    if (!(dir_inode.i_flags & EXT4_EXTENTS_FL)) {
        return 0;
    }

    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)dir_inode.i_block;
    if (hdr->eh_magic != EXT4_EXTENT_HEADER_MAGIC || hdr->eh_depth != 0) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    Ext4Extent *ext = (Ext4Extent *)(hdr + 1);

    uint32_t out_pos = 0;
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

                if (entry->inode != 0 && entry->name_len > 0) {
                    if (entry->name_len == 1 && entry->name[0] == '.') {
                        offset += entry->rec_len;
                        continue;
                    }
                    if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') {
                        offset += entry->rec_len;
                        continue;
                    }

                    if (out_pos + entry->name_len + 2 >= max_size) {
                        out[out_pos] = 0;
                        return 1;
                    }

                    for (uint8_t n = 0; n < entry->name_len; n++) {
                        out[out_pos++] = entry->name[n];
                    }
                    out[out_pos++] = '\n';
                }

                offset += entry->rec_len;
            }
        }
    }

    if (out_pos < max_size) {
        out[out_pos] = 0;
    }
    return 1;
}

int ext4_write_file(Ext4Fs *fs, const char *path, const void *buffer, uint32_t size) {
    if (!fs || !path || path[0] != '/') {
        return 0;
    }

    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last = p;
        }
    }

    const char *name = last;
    while (*name == '/') {
        name++;
    }
    if (name[0] == 0) {
        return 0;
    }

    char parent_path[256];
    uint32_t parent_len = (uint32_t)(name - path);
    if (parent_len == 0) {
        parent_path[0] = '/';
        parent_path[1] = 0;
    } else {
        uint32_t copy_len = parent_len;
        if (copy_len >= sizeof(parent_path)) {
            return 0;
        }
        for (uint32_t i = 0; i < copy_len; i++) {
            parent_path[i] = path[i];
        }
        if (parent_len > 1 && parent_path[parent_len - 1] == '/') {
            parent_len--;
        }
        parent_path[parent_len] = 0;
    }

    uint32_t parent_inode = 0;
    uint8_t parent_type = 0;
    if (!ext4_resolve_path(fs, parent_path, &parent_inode, &parent_type) || parent_type != EXT4_FT_DIR) {
        return 0;
    }

    uint32_t inode_num = 0;
    uint8_t inode_type = 0;
    int exists = 0;
    {
        Ext4Inode parent_inode_data;
        if (!ext4_read_inode(fs, parent_inode, &parent_inode_data, 0)) {
            return 0;
        }
        if (ext4_find_in_dir(fs, &parent_inode_data, name, str_len(name), &inode_num, &inode_type)) {
            exists = 1;
        }
    }

    if (!exists) {
        if (!ext4_alloc_inode(fs, &inode_num)) {
            return 0;
        }

        Ext4Inode new_inode;
        for (int i = 0; i < (int)sizeof(new_inode); i++) {
            ((uint8_t *)&new_inode)[i] = 0;
        }
        new_inode.i_mode = 0x81A4;
        new_inode.i_links_count = 1;
        new_inode.i_flags = EXT4_EXTENTS_FL;

        if (!ext4_write_inode(fs, inode_num, &new_inode)) {
            return 0;
        }

        if (!ext4_add_dir_entry(fs, parent_inode, name, EXT4_FT_REG_FILE, inode_num)) {
            return 0;
        }
    }

    Ext4Inode file_inode;
    if (!ext4_read_inode(fs, inode_num, &file_inode, 0)) {
        return 0;
    }

    uint32_t block_size = fs->sb.block_size;
    uint32_t blocks_needed = (size + block_size - 1) / block_size;

    if (blocks_needed > 0) {
        int reused = 0;
        if ((file_inode.i_flags & EXT4_EXTENTS_FL) && ((Ext4ExtentHeader *)file_inode.i_block)->eh_magic == EXT4_EXTENT_HEADER_MAGIC) {
            Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)file_inode.i_block;
            if (hdr->eh_depth == 0 && hdr->eh_entries >= 1) {
                Ext4Extent *ext = (Ext4Extent *)(hdr + 1);
                uint32_t existing_len = ext[0].ee_len & 0x7FFF;
                if (existing_len >= blocks_needed) {
                    if (!ext4_write_extent_blocks(fs, &file_inode, 0, buffer, size)) {
                        return 0;
                    }
                    reused = 1;
                }
            }
        }

        if (!reused) {
            uint32_t start_block = 0;
            if (!ext4_alloc_block_run(fs, blocks_needed, &start_block)) {
                return 0;
            }

            Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)file_inode.i_block;
            hdr->eh_magic = EXT4_EXTENT_HEADER_MAGIC;
            hdr->eh_entries = 1;
            hdr->eh_max = 4;
            hdr->eh_depth = 0;
            hdr->eh_generation = 0;

            Ext4Extent *ext = (Ext4Extent *)(hdr + 1);
            ext[0].ee_block = 0;
            ext[0].ee_len = (uint16_t)blocks_needed;
            ext[0].ee_start_hi = (uint16_t)(start_block >> 32);
            ext[0].ee_start_lo = (uint32_t)(start_block & 0xFFFFFFFFU);

            file_inode.i_flags |= EXT4_EXTENTS_FL;

            if (!ext4_write_extent_blocks(fs, &file_inode, 0, buffer, size)) {
                return 0;
            }
        }
    }

    file_inode.i_size_lo = size;
    file_inode.i_size_high = 0;
    file_inode.i_blocks_lo = blocks_needed * (block_size / 512);

    return ext4_write_inode(fs, inode_num, &file_inode);
}
