#ifndef KAGAMI_EXT4_H
#define KAGAMI_EXT4_H

#include "../types.h"
#include "../storage/block.h"

#define EXT4_SUPERBLOCK_LBA 2
#define EXT4_SUPER_MAGIC 0xEF53

typedef struct {
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t blocks_per_group;
    uint64_t total_blocks;
    uint64_t total_inodes;
    uint32_t features_compat;
    uint32_t features_incompat;
    uint32_t features_ro_compat;
    uint32_t first_data_block;
    uint32_t group_desc_size;
} Ext4SuperblockInfo;

typedef struct {
    BlockDevice *device;
    Ext4SuperblockInfo sb;
    uint64_t partition_lba;
} Ext4Fs;

int ext4_mount(Ext4Fs *fs, BlockDevice *dev, uint64_t partition_lba);
int ext4_read_file(Ext4Fs *fs, const char *path, void *buffer, uint32_t max_size, uint32_t *out_size);

#endif
