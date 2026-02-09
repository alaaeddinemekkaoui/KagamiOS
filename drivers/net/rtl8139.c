#include "rtl8139.h"
#include "drivers/bus/pci.h"
#include "core/io.h"
#include "serial.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

#define RTL_REG_IDR0   0x00
#define RTL_REG_TSD0   0x10
#define RTL_REG_TSAD0  0x20
#define RTL_REG_RBSTART 0x30
#define RTL_REG_CR     0x37
#define RTL_REG_CAPR   0x38
#define RTL_REG_ISR    0x3E
#define RTL_REG_IMR    0x3C
#define RTL_REG_RCR    0x44
#define RTL_REG_CONFIG1 0x52

#define RTL_CR_RST 0x10
#define RTL_CR_RE  0x08
#define RTL_CR_TE  0x04

#define RTL_ISR_ROK 0x01
#define RTL_ISR_RER 0x02
#define RTL_ISR_TOK 0x04
#define RTL_ISR_TER 0x08

#define RTL_RCR_AAP  (1 << 0)
#define RTL_RCR_APM  (1 << 1)
#define RTL_RCR_AM   (1 << 2)
#define RTL_RCR_AB   (1 << 3)
#define RTL_RCR_WRAP (1 << 7)

static uint8_t rx_buffer[8192 + 16 + 1500] __attribute__((aligned(16)));
static uint8_t tx_buffer[4][RTL8139_MAX_FRAME] __attribute__((aligned(4)));
static uint32_t tx_cur = 0;
static uint16_t rx_offset = 0;

static int rtl_find_device(uint8_t *bus, uint8_t *slot, uint8_t *func) {
    for (uint16_t b = 0; b < 256; b++) {
        for (uint8_t s = 0; s < 32; s++) {
            uint32_t vendor_device = pci_read32((uint8_t)b, s, 0, 0x00);
            uint16_t vendor = (uint16_t)(vendor_device & 0xFFFF);
            if (vendor == 0xFFFF) {
                continue;
            }

            uint8_t functions = 1;
            uint32_t header_reg = pci_read32((uint8_t)b, s, 0, 0x0C);
            if (header_reg & (1 << 23)) {
                functions = 8;
            }

            for (uint8_t f = 0; f < functions; f++) {
                vendor_device = pci_read32((uint8_t)b, s, f, 0x00);
                vendor = (uint16_t)(vendor_device & 0xFFFF);
                uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFF);
                if (vendor == RTL8139_VENDOR && device == RTL8139_DEVICE) {
                    *bus = (uint8_t)b;
                    *slot = s;
                    *func = f;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void rtl_write8(uint32_t base, uint32_t reg, uint8_t value) {
    outb((uint16_t)(base + reg), value);
}

static void rtl_write16(uint32_t base, uint32_t reg, uint16_t value) {
    outw((uint16_t)(base + reg), value);
}

static void rtl_write32(uint32_t base, uint32_t reg, uint32_t value) {
    outl((uint16_t)(base + reg), value);
}

static uint8_t rtl_read8(uint32_t base, uint32_t reg) {
    return inb((uint16_t)(base + reg));
}

static uint16_t rtl_read16(uint32_t base, uint32_t reg) {
    return inw((uint16_t)(base + reg));
}

static uint32_t rtl_read32(uint32_t base, uint32_t reg) {
    return inl((uint16_t)(base + reg));
}

int rtl8139_init(Rtl8139Device *dev) {
    uint8_t bus = 0, slot = 0, func = 0;
    if (!rtl_find_device(&bus, &slot, &func)) {
        serial_write("RTL8139: device not found\n");
        return 0;
    }

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    uint32_t io_base = bar0 & ~0x3U;

    uint32_t command = pci_read32(bus, slot, func, 0x04);
    command |= (1 << 2) | (1 << 0);
    pci_write32(bus, slot, func, 0x04, command);

    rtl_write8(io_base, RTL_REG_CONFIG1, 0x00);

    rtl_write8(io_base, RTL_REG_CR, RTL_CR_RST);
    while (rtl_read8(io_base, RTL_REG_CR) & RTL_CR_RST) {
    }

    rtl_write32(io_base, RTL_REG_RBSTART, (uint32_t)(uintptr_t)rx_buffer);

    rtl_write16(io_base, RTL_REG_IMR, RTL_ISR_ROK | RTL_ISR_TOK | RTL_ISR_RER | RTL_ISR_TER);
    rtl_write32(io_base, RTL_REG_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP);
    rtl_write8(io_base, RTL_REG_CR, RTL_CR_RE | RTL_CR_TE);

    for (int i = 0; i < 6; i++) {
        dev->mac[i] = rtl_read8(io_base, RTL_REG_IDR0 + i);
    }

    dev->io_base = io_base;
    rx_offset = 0;
    tx_cur = 0;

    serial_write("RTL8139: initialized\n");
    return 1;
}

int rtl8139_send(Rtl8139Device *dev, const void *data, uint32_t length) {
    if (!dev || length == 0 || length > RTL8139_MAX_FRAME) {
        return 0;
    }

    for (uint32_t i = 0; i < length; i++) {
        tx_buffer[tx_cur][i] = ((const uint8_t *)data)[i];
    }

    uint32_t tx_addr = (uint32_t)(uintptr_t)tx_buffer[tx_cur];
    rtl_write32(dev->io_base, RTL_REG_TSAD0 + (tx_cur * 4), tx_addr);
    rtl_write32(dev->io_base, RTL_REG_TSD0 + (tx_cur * 4), length);

    tx_cur = (tx_cur + 1) % 4;
    return 1;
}

int rtl8139_poll(Rtl8139Device *dev, uint8_t *out_buf, uint32_t max_len, uint32_t *out_len) {
    if (!dev || !out_buf || !out_len) {
        return 0;
    }

    uint16_t isr = rtl_read16(dev->io_base, RTL_REG_ISR);
    if (!(isr & RTL_ISR_ROK)) {
        return 0;
    }

    rtl_write16(dev->io_base, RTL_REG_ISR, RTL_ISR_ROK);

    uint16_t offset = rx_offset;
    uint16_t *header = (uint16_t *)(rx_buffer + offset);
    uint16_t status = header[0];
    uint16_t length = header[1];

    if (!(status & 0x01)) {
        return 0;
    }

    if (length > max_len) {
        length = (uint16_t)max_len;
    }

    uint8_t *pkt = rx_buffer + offset + 4;
    for (uint16_t i = 0; i < length; i++) {
        out_buf[i] = pkt[i];
    }

    *out_len = length;

    offset = (uint16_t)(offset + length + 4 + 3) & ~3U;
    if (offset >= 8192) {
        offset -= 8192;
    }
    rx_offset = offset;
    rtl_write16(dev->io_base, RTL_REG_CAPR, rx_offset - 16);

    return 1;
}
