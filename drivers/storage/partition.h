#ifndef KAGAMI_PARTITION_H
#define KAGAMI_PARTITION_H

#include "types.h"
#include "drivers/storage/block.h"

typedef struct {
    uint64_t first_lba;
    uint64_t last_lba;
} PartitionInfo;

int gpt_find_linux_partition(BlockDevice *dev, PartitionInfo *out);
int mbr_find_linux_partition(BlockDevice *dev, PartitionInfo *out);
int find_linux_partition(BlockDevice *dev, PartitionInfo *out);
int raw_find_ext4(BlockDevice *dev, PartitionInfo *out);

#endif
