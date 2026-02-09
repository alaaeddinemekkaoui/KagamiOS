#include "net.h"
#include "drivers/net/rtl8139.h"
#include "serial.h"

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800

#define ARP_HTYPE_ETH 1
#define ARP_PTYPE_IP  0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

typedef struct {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed)) EthHeader;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} __attribute__((packed)) ArpPacket;

typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed)) Ipv4Header;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) IcmpHeader;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
} ArpEntry;

static Rtl8139Device g_nic;
static int g_net_ready = 0;

static uint32_t g_ip = 0;
static uint32_t g_netmask = 0;
static uint32_t g_gateway = 0;
static ArpEntry g_arp[8];
static int g_arp_count = 0;

static uint16_t swap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

static uint16_t checksum16(const void *data, uint32_t len) {
    const uint16_t *buf = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len) {
        sum += *(const uint8_t *)buf;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void arp_cache_set(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ip == ip) {
            for (int j = 0; j < 6; j++) {
                g_arp[i].mac[j] = mac[j];
            }
            return;
        }
    }
    if (g_arp_count < 8) {
        g_arp[g_arp_count].ip = ip;
        for (int j = 0; j < 6; j++) {
            g_arp[g_arp_count].mac[j] = mac[j];
        }
        g_arp_count++;
    }
}

static int arp_cache_get(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ip == ip) {
            for (int j = 0; j < 6; j++) {
                mac[j] = g_arp[i].mac[j];
            }
            return 1;
        }
    }
    return 0;
}

static void net_send_frame(const uint8_t *dst, uint16_t type, const void *payload, uint32_t len) {
    uint8_t frame[RTL8139_MAX_FRAME];
    EthHeader *eth = (EthHeader *)frame;
    for (int i = 0; i < 6; i++) {
        eth->dst[i] = dst[i];
        eth->src[i] = g_nic.mac[i];
    }
    eth->type = swap16(type);

    uint8_t *data = frame + sizeof(EthHeader);
    for (uint32_t i = 0; i < len; i++) {
        data[i] = ((const uint8_t *)payload)[i];
    }

    uint32_t total = sizeof(EthHeader) + len;
    if (total < 60) {
        total = 60;
    }
    rtl8139_send(&g_nic, frame, total);
}

static void net_handle_arp(const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(EthHeader) + sizeof(ArpPacket)) {
        return;
    }

    const ArpPacket *arp = (const ArpPacket *)(pkt + sizeof(EthHeader));
    if (swap16(arp->htype) != ARP_HTYPE_ETH || swap16(arp->ptype) != ARP_PTYPE_IP) {
        return;
    }

    arp_cache_set(arp->spa, arp->sha);

    if (swap16(arp->oper) == ARP_OP_REQUEST && arp->tpa == g_ip) {
        ArpPacket reply;
        reply.htype = swap16(ARP_HTYPE_ETH);
        reply.ptype = swap16(ARP_PTYPE_IP);
        reply.hlen = 6;
        reply.plen = 4;
        reply.oper = swap16(ARP_OP_REPLY);
        for (int i = 0; i < 6; i++) {
            reply.sha[i] = g_nic.mac[i];
            reply.tha[i] = arp->sha[i];
        }
        reply.spa = g_ip;
        reply.tpa = arp->spa;
        net_send_frame(arp->sha, ETH_TYPE_ARP, &reply, sizeof(ArpPacket));
    }
}

static void net_handle_ip(const uint8_t *pkt, uint32_t len) {
    if (len < sizeof(EthHeader) + sizeof(Ipv4Header)) {
        return;
    }

    const Ipv4Header *ip = (const Ipv4Header *)(pkt + sizeof(EthHeader));
    if ((ip->ver_ihl >> 4) != 4) {
        return;
    }

    uint16_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (len < sizeof(EthHeader) + ihl + sizeof(IcmpHeader)) {
        return;
    }

    if (ip->proto == 1) {
        const IcmpHeader *icmp = (const IcmpHeader *)((const uint8_t *)ip + ihl);
        if (icmp->type == ICMP_ECHO_REQUEST && ip->dst == g_ip) {
            uint32_t payload_len = swap16(ip->total_len) - ihl - sizeof(IcmpHeader);
            uint8_t reply_buf[RTL8139_MAX_FRAME];
            Ipv4Header *rip = (Ipv4Header *)(reply_buf + sizeof(EthHeader));
            IcmpHeader *ricmp = (IcmpHeader *)((uint8_t *)rip + ihl);

            rip->ver_ihl = 0x45;
            rip->tos = 0;
            rip->total_len = swap16((uint16_t)(ihl + sizeof(IcmpHeader) + payload_len));
            rip->id = 0;
            rip->flags_frag = 0;
            rip->ttl = 64;
            rip->proto = 1;
            rip->src = g_ip;
            rip->dst = ip->src;
            rip->checksum = 0;
            rip->checksum = checksum16(rip, ihl);

            ricmp->type = ICMP_ECHO_REPLY;
            ricmp->code = 0;
            ricmp->id = icmp->id;
            ricmp->seq = icmp->seq;
            ricmp->checksum = 0;

            const uint8_t *payload = (const uint8_t *)icmp + sizeof(IcmpHeader);
            uint8_t *out_payload = (uint8_t *)ricmp + sizeof(IcmpHeader);
            for (uint32_t i = 0; i < payload_len; i++) {
                out_payload[i] = payload[i];
            }

            ricmp->checksum = checksum16(ricmp, sizeof(IcmpHeader) + payload_len);

            EthHeader *eth = (EthHeader *)reply_buf;
            const EthHeader *in_eth = (const EthHeader *)pkt;
            for (int i = 0; i < 6; i++) {
                eth->dst[i] = in_eth->src[i];
                eth->src[i] = g_nic.mac[i];
            }
            eth->type = swap16(ETH_TYPE_IP);

            uint32_t frame_len = sizeof(EthHeader) + ihl + sizeof(IcmpHeader) + payload_len;
            if (frame_len < 60) {
                frame_len = 60;
            }
            rtl8139_send(&g_nic, reply_buf, frame_len);
        }
    }
}

static void net_poll(void) {
    uint8_t buf[RTL8139_MAX_FRAME];
    uint32_t len = 0;

    if (rtl8139_poll(&g_nic, buf, sizeof(buf), &len)) {
        if (len < sizeof(EthHeader)) {
            return;
        }
        EthHeader *eth = (EthHeader *)buf;
        uint16_t type = swap16(eth->type);
        if (type == ETH_TYPE_ARP) {
            net_handle_arp(buf, len);
        } else if (type == ETH_TYPE_IP) {
            net_handle_ip(buf, len);
        }
    }
}

static int arp_resolve(uint32_t ip, uint8_t *mac_out) {
    if (arp_cache_get(ip, mac_out)) {
        return 1;
    }

    ArpPacket req;
    req.htype = swap16(ARP_HTYPE_ETH);
    req.ptype = swap16(ARP_PTYPE_IP);
    req.hlen = 6;
    req.plen = 4;
    req.oper = swap16(ARP_OP_REQUEST);
    for (int i = 0; i < 6; i++) {
        req.sha[i] = g_nic.mac[i];
        req.tha[i] = 0;
    }
    req.spa = g_ip;
    req.tpa = ip;

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_send_frame(broadcast, ETH_TYPE_ARP, &req, sizeof(ArpPacket));

    for (int i = 0; i < 50000; i++) {
        net_poll();
        if (arp_cache_get(ip, mac_out)) {
            return 1;
        }
    }

    return 0;
}

int net_init(void) {
    if (!rtl8139_init(&g_nic)) {
        return 0;
    }

    g_ip = swap32(0x0A00020F);      /* 10.0.2.15 */
    g_netmask = swap32(0xFFFFFF00); /* 255.255.255.0 */
    g_gateway = swap32(0x0A000202); /* 10.0.2.2 */

    g_net_ready = 1;
    return 1;
}

void net_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    g_ip = ip;
    g_netmask = netmask;
    g_gateway = gateway;
}

void net_get_ip(uint32_t *ip, uint32_t *netmask, uint32_t *gateway) {
    if (ip) {
        *ip = g_ip;
    }
    if (netmask) {
        *netmask = g_netmask;
    }
    if (gateway) {
        *gateway = g_gateway;
    }
}

int net_parse_ipv4(const char *str, uint32_t *out_ip) {
    uint32_t parts[4] = {0};
    int part = 0;
    uint32_t value = 0;

    for (int i = 0; str && str[i]; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (uint32_t)(c - '0');
            if (value > 255) {
                return 0;
            }
        } else if (c == '.') {
            if (part >= 4) {
                return 0;
            }
            parts[part++] = value;
            value = 0;
        } else {
            return 0;
        }
    }

    if (part != 3) {
        return 0;
    }
    parts[3] = value;

    uint32_t ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    *out_ip = swap32(ip);
    return 1;
}

void net_ip_to_str(uint32_t ip, char *out, uint32_t max_len) {
    uint32_t v = swap32(ip);
    uint8_t b0 = (v >> 24) & 0xFF;
    uint8_t b1 = (v >> 16) & 0xFF;
    uint8_t b2 = (v >> 8) & 0xFF;
    uint8_t b3 = v & 0xFF;

    char buf[16];
    int len = 0;

    uint8_t bytes[4] = {b0, b1, b2, b3};
    for (int i = 0; i < 4; i++) {
        uint8_t val = bytes[i];
        if (val >= 100) {
            buf[len++] = '0' + (val / 100);
            buf[len++] = '0' + ((val / 10) % 10);
            buf[len++] = '0' + (val % 10);
        } else if (val >= 10) {
            buf[len++] = '0' + (val / 10);
            buf[len++] = '0' + (val % 10);
        } else {
            buf[len++] = '0' + val;
        }
        if (i != 3) {
            buf[len++] = '.';
        }
    }
    buf[len] = 0;

    if (max_len == 0) {
        return;
    }

    uint32_t i = 0;
    while (buf[i] && i < max_len - 1) {
        out[i] = buf[i];
        i++;
    }
    out[i] = 0;
}

int net_ping(const char *ip_str) {
    if (!g_net_ready) {
        return 0;
    }

    uint32_t dest_ip = 0;
    if (!net_parse_ipv4(ip_str, &dest_ip)) {
        return 0;
    }

    uint32_t target = dest_ip;
    if ((g_ip & g_netmask) != (dest_ip & g_netmask)) {
        if (g_gateway == 0) {
            return 0;
        }
        target = g_gateway;
    }

    uint8_t dst_mac[6];
    if (!arp_resolve(target, dst_mac)) {
        return 0;
    }

    uint8_t packet[RTL8139_MAX_FRAME];
    EthHeader *eth = (EthHeader *)packet;
    for (int i = 0; i < 6; i++) {
        eth->dst[i] = dst_mac[i];
        eth->src[i] = g_nic.mac[i];
    }
    eth->type = swap16(ETH_TYPE_IP);

    Ipv4Header *ip = (Ipv4Header *)(packet + sizeof(EthHeader));
    IcmpHeader *icmp = (IcmpHeader *)((uint8_t *)ip + sizeof(Ipv4Header));
    const char payload[] = "Kagami";

    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = swap16((uint16_t)(sizeof(Ipv4Header) + sizeof(IcmpHeader) + sizeof(payload)));
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = 1;
    ip->src = g_ip;
    ip->dst = dest_ip;
    ip->checksum = 0;
    ip->checksum = checksum16(ip, sizeof(Ipv4Header));

    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = swap16(0x1234);
    icmp->seq = swap16(1);
    icmp->checksum = 0;

    uint8_t *pl = (uint8_t *)icmp + sizeof(IcmpHeader);
    for (uint32_t i = 0; i < sizeof(payload); i++) {
        pl[i] = (uint8_t)payload[i];
    }

    icmp->checksum = checksum16(icmp, sizeof(IcmpHeader) + sizeof(payload));

    uint32_t total_len = sizeof(EthHeader) + sizeof(Ipv4Header) + sizeof(IcmpHeader) + sizeof(payload);
    if (total_len < 60) {
        total_len = 60;
    }
    rtl8139_send(&g_nic, packet, total_len);

    for (int i = 0; i < 200000; i++) {
        uint8_t buf[RTL8139_MAX_FRAME];
        uint32_t len = 0;
        if (rtl8139_poll(&g_nic, buf, sizeof(buf), &len)) {
            if (len < sizeof(EthHeader) + sizeof(Ipv4Header) + sizeof(IcmpHeader)) {
                continue;
            }
            EthHeader *reth = (EthHeader *)buf;
            if (swap16(reth->type) != ETH_TYPE_IP) {
                continue;
            }
            Ipv4Header *rip = (Ipv4Header *)(buf + sizeof(EthHeader));
            if (rip->proto != 1 || rip->src != dest_ip) {
                continue;
            }
            IcmpHeader *ricmp = (IcmpHeader *)((uint8_t *)rip + sizeof(Ipv4Header));
            if (ricmp->type == ICMP_ECHO_REPLY) {
                return 1;
            }
        }
        net_poll();
    }

    return 0;
}
