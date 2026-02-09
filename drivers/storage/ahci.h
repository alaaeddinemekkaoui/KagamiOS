#ifndef KAGAMI_AHCI_H
#define KAGAMI_AHCI_H

#include "types.h"

typedef struct BlockDevice BlockDevice;

int ahci_init(void);
BlockDevice *ahci_get_device(void);

#endif
