#ifndef KAGAMI_BLOCK_H
#define KAGAMI_BLOCK_H

#include "types.h"

#define BLOCK_MAX_DEVICES 8
#define BLOCK_SECTOR_SIZE 512

typedef struct BlockDevice BlockDevice;

struct BlockDevice {
    const char *name;
    uint32_t sector_size;
    uint64_t total_sectors;
    void *driver_data;

    int (*read)(BlockDevice *dev, uint64_t lba, uint32_t count, void *buffer);
    int (*write)(BlockDevice *dev, uint64_t lba, uint32_t count, const void *buffer);
};

int block_register(BlockDevice *dev);
BlockDevice *block_get(int index);
int block_count(void);

#endif
