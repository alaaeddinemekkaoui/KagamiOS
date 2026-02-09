#ifndef KAGAMI_PCI_H
#define KAGAMI_PCI_H

#include "types.h"

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
} PciDevice;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, PciDevice *out);
int pci_enumerate(PciDevice *out, int max);

#endif
