#include "pci.h"
#include "../core/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)(1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)(1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_get_device(uint8_t bus, uint8_t slot, uint8_t func, PciDevice *out) {
    uint32_t vendor_device = pci_read32(bus, slot, func, 0x00);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFF);
    if (vendor == 0xFFFF) {
        return 0;
    }

    uint32_t class_reg = pci_read32(bus, slot, func, 0x08);
    uint32_t header_reg = pci_read32(bus, slot, func, 0x0C);

    out->bus = bus;
    out->slot = slot;
    out->func = func;
    out->vendor_id = vendor;
    out->device_id = (uint16_t)((vendor_device >> 16) & 0xFFFF);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFF);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    out->prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    out->header_type = (uint8_t)((header_reg >> 16) & 0xFF);
    return 1;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, PciDevice *out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            PciDevice dev;
            if (!pci_get_device((uint8_t)bus, slot, 0, &dev)) {
                continue;
            }

            uint8_t functions = (dev.header_type & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < functions; func++) {
                if (!pci_get_device((uint8_t)bus, slot, func, &dev)) {
                    continue;
                }

                if (dev.class_code == class_code && dev.subclass == subclass && dev.prog_if == prog_if) {
                    if (out) {
                        *out = dev;
                    }
                    return 1;
                }
            }
        }
    }
    return 0;
}
