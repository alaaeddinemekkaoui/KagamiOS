#ifndef KAGAMI_NET_H
#define KAGAMI_NET_H

#include "types.h"

int net_init(void);
void net_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway);
void net_get_ip(uint32_t *ip, uint32_t *netmask, uint32_t *gateway);
int net_ping(const char *ip_str);
int net_parse_ipv4(const char *str, uint32_t *out_ip);
void net_ip_to_str(uint32_t ip, char *out, uint32_t max_len);

#endif
