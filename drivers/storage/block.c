#include "block.h"

static BlockDevice *devices[BLOCK_MAX_DEVICES];
static int device_count = 0;

int block_register(BlockDevice *dev) {
    if (!dev || device_count >= BLOCK_MAX_DEVICES) {
        return 0;
    }
    devices[device_count++] = dev;
    return 1;
}

BlockDevice *block_get(int index) {
    if (index < 0 || index >= device_count) {
        return 0;
    }
    return devices[index];
}

int block_count(void) {
    return device_count;
}
