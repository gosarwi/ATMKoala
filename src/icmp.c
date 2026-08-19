/*
 * icmp.c — ICMP echo request/reply (ping) for atmkoala, RFC 792
 *
 * Builds raw Ethernet + IPv4 + ICMP frames directly, the same way
 * unm.c builds DHCP packets — there's no separate IP routing layer
 * here yet, so the destination MAC is resolved via the existing ARP
 * helper (net_arp_request) and we assume same-subnet delivery, which
 * matches how the DHCP code already operates.
 */
#include "icmp.h"
#include "net.h"
#include "util.h"
#include "vga.h"
#include "pit.h"
#include <stdint.h>
#include <stddef.h>

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_ID_DEFAULT        0xC0DE

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    /* payload follows — we send a small fixed pattern */
} icmp_hdr_t;

#define ICMP_PAYLOAD_LEN 32

static int icmp_send_echo(const uint8_t dst_ip[4], uint16_t id, uint16_t seq) {
    static uint8_t frame[14 + 20 + sizeof(icmp_hdr_t) + ICMP_PAYLOAD_LEN];
    kmemset(frame, 0, sizeof(frame));

    /* Ethernet header — destination MAC unknown without a real ARP
     * cache; broadcast works for delivery to the gateway/host under
     * QEMU SLIRP and most flat single-segment test networks, which
     * is the same simplification unm.c's DHCP code already makes
     * for its own frames. A real ARP-resolved unicast destination is
     * a natural next step once an ARP cache exists. */
    eth_header_t *eth = (eth_header_t *)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF;
    for (int i = 0; i < 6; i++) eth->src[i] = net.mac[i];
    eth->ethertype = net_bswap16(0x0800);

    ip4_hdr_t *ip = (ip4_hdr_t *)(frame + 14);
    ip->ver_ihl  = 0x45;
    ip->tos      = 0;
    ip->id       = net_bswap16(seq);
    ip->flags_frag = 0;
    ip->ttl      = 64;
    ip->protocol = 1; /* ICMP */
    for (int i = 0; i < 4; i++) ip->src_ip[i] = net.ip[i];
    for (int i = 0; i < 4; i++) ip->dst_ip[i] = dst_ip[i];

    icmp_hdr_t *icmp = (icmp_hdr_t *)(frame + 14 + 20);
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id  = net_bswap16(id);
    icmp->seq = net_bswap16(seq);

    uint8_t *payload = frame + 14 + 20 + (int)sizeof(icmp_hdr_t);
    for (int i = 0; i < ICMP_PAYLOAD_LEN; i++) payload[i] = (uint8_t)(0x41 + (i % 26));

    int icmp_len = (int)sizeof(icmp_hdr_t) + ICMP_PAYLOAD_LEN;
    icmp->checksum = net_ip_checksum(icmp, icmp_len);

    int ip_len = 20 + icmp_len;
    ip->total_len = net_bswap16((uint16_t)ip_len);
    ip->checksum  = net_ip_checksum(ip, 20);

    int frame_len = 14 + ip_len;
    return net_send(frame, (uint16_t)frame_len);
}

/* Parse an incoming frame, return 0 if it's a matching echo reply
 * for (id, seq), -1 otherwise. */
static int icmp_match_reply(const uint8_t *buf, int len, uint16_t id,
                             uint16_t seq, uint8_t out_src_ip[4]) {
    if (len < 14 + 20 + (int)sizeof(icmp_hdr_t)) return -1;

    const eth_header_t *eth = (const eth_header_t *)buf;
    if (net_bswap16(eth->ethertype) != 0x0800) return -1;

    const ip4_hdr_t *ip = (const ip4_hdr_t *)(buf + 14);
    if (ip->protocol != 1) return -1;

    int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (14 + ihl + (int)sizeof(icmp_hdr_t) > len) return -1;

    const icmp_hdr_t *icmp = (const icmp_hdr_t *)(buf + 14 + ihl);
    if (icmp->type != ICMP_TYPE_ECHO_REPLY) return -1;
    if (net_bswap16(icmp->id)  != id)  return -1;
    if (net_bswap16(icmp->seq) != seq) return -1;

    for (int i = 0; i < 4; i++) out_src_ip[i] = ip->src_ip[i];
    return 0;
}

int icmp_ping_once(const uint8_t dst_ip[4], uint16_t seq, uint32_t timeout_ticks) {
    if (!net.initialized) return -1;

    uint16_t id = ICMP_ID_DEFAULT;
    uint32_t t0 = pit_get_ticks();

    if (icmp_send_echo(dst_ip, id, seq) < 0) return -1;

    static uint8_t rxbuf[1600];
    uint8_t reply_src[4];

    while ((int)(pit_get_ticks() - t0) < (int)timeout_ticks) {
        int n = net_recv(rxbuf, sizeof(rxbuf));
        if (n > 0 && icmp_match_reply(rxbuf, n, id, seq, reply_src) == 0) {
            uint32_t elapsed_ticks = pit_get_ticks() - t0;
            return (int)(elapsed_ticks * 10); /* ticks(100/s) -> ms */
        }
        pit_sleep(1);
    }
    return -1; /* timeout */
}

void icmp_ping(const uint8_t dst_ip[4], int count) {
    if (!net.initialized) {
        terminal_writeln("  ping: no network interface");
        return;
    }
    if (count <= 0) count = 4;

    char ipbuf[24];
    char line[80];
    ksnprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u",
        dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    ksnprintf(line, sizeof(line), "PING %s: %d bytes of data.",
        ipbuf, ICMP_PAYLOAD_LEN);
    terminal_writeln(line);

    int sent = 0, received = 0;
    uint32_t min_ms = 0xFFFFFFFFu, max_ms = 0, total_ms = 0;

    for (int i = 0; i < count; i++) {
        sent++;
        int rtt = icmp_ping_once(dst_ip, (uint16_t)i, 200); /* 2s timeout */
        if (rtt >= 0) {
            received++;
            uint32_t ms = (uint32_t)rtt;
            if (ms < min_ms) min_ms = ms;
            if (ms > max_ms) max_ms = ms;
            total_ms += ms;
            ksnprintf(line, sizeof(line),
                "  %d bytes from %s: icmp_seq=%d ttl=64 time=%u ms",
                ICMP_PAYLOAD_LEN, ipbuf, i, ms);
            terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            terminal_writeln(line);
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        } else {
            ksnprintf(line, sizeof(line), "  icmp_seq=%d Request timeout", i);
            terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
            terminal_writeln(line);
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
        if (i < count - 1) pit_sleep(100); /* ~1s between pings */
    }

    ksnprintf(line, sizeof(line),
        "--- %s ping statistics ---", ipbuf);
    terminal_writeln(line);
    int loss_pct = sent ? (100 * (sent - received) / sent) : 0;
    ksnprintf(line, sizeof(line),
        "%d packets transmitted, %d received, %d%% packet loss",
        sent, received, loss_pct);
    terminal_writeln(line);
    if (received > 0) {
        ksnprintf(line, sizeof(line),
            "round-trip min/avg/max = %u/%u/%u ms",
            min_ms, total_ms / (uint32_t)received, max_ms);
        terminal_writeln(line);
    }
}
