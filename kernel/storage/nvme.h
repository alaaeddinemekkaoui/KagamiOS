#ifndef KAGAMI_NVME_H
#define KAGAMI_NVME_H

#include "../types.h"

typedef struct BlockDevice BlockDevice;

int nvme_init(void);
BlockDevice *nvme_get_device(void);

#endif
