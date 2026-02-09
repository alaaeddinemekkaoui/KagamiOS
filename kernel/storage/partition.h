#ifndef KAGAMI_PARTITION_H
#define KAGAMI_PARTITION_H

#include "../types.h"
#include "block.h"

typedef struct {
    uint64_t first_lba;
    uint64_t last_lba;
} PartitionInfo;

int gpt_find_linux_partition(BlockDevice *dev, PartitionInfo *out);

#endif
