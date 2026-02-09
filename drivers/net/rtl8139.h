#ifndef KAGAMI_RTL8139_H
#define KAGAMI_RTL8139_H

#include "types.h"

#define RTL8139_MAX_FRAME 1600

typedef struct {
    uint8_t mac[6];
    uint32_t io_base;
} Rtl8139Device;

int rtl8139_init(Rtl8139Device *dev);
int rtl8139_send(Rtl8139Device *dev, const void *data, uint32_t length);
int rtl8139_poll(Rtl8139Device *dev, uint8_t *out_buf, uint32_t max_len, uint32_t *out_len);

#endif
