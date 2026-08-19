/*
 * dns.c — DNS resolver for atmkoala, RFC 1035 (A records, UDP transport)
 *
 * Builds a single iterative query, sends it to the given DNS server
 * over UDP/53, and parses the response — including DNS name
 * compression pointers (RFC 1035 §4.1.4), which real-world servers
 * use even in small responses, so skipping that handling would make
 * this resolver fail against most actual nameservers, not just a
 * theoretical edge case.
 */
#include "dns.h"
#include "net.h"
#include "util.h"
#include "pit.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define DNS_PORT        53
#define DNS_CLIENT_PORT 33533   /* arbitrary high ephemeral port */
#define DNS_QTYPE_A     1
#define DNS_QCLASS_IN   1
#define DNS_MAX_LABEL   63
#define DNS_MAX_NAME    255

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_hdr_t;

#define DNS_FLAG_QR     0x8000  /* 1 = response */
#define DNS_FLAG_RD     0x0100  /* recursion desired */
#define DNS_RCODE_MASK  0x000F

/* ── Encode "www.example.com" into DNS wire-format QNAME ─────── */
static int dns_encode_name(const char *host, uint8_t *out, int outsz) {
    int pos = 0;
    const char *label_start = host;

    while (1) {
        const char *p = label_start;
        while (*p && *p != '.') p++;
        int label_len = (int)(p - label_start);

        if (label_len == 0 || label_len > DNS_MAX_LABEL) return -1;
        if (pos + 1 + label_len >= outsz) return -1;

        out[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) out[pos++] = (uint8_t)label_start[i];

        if (*p == '.') { label_start = p + 1; continue; }
        break; /* hit end of string */
    }

    if (pos + 1 >= outsz) return -1;
    out[pos++] = 0; /* root label terminator */
    return pos;
}

/* ── Skip over a (possibly compressed) name, return bytes consumed
 * starting at `off`, or -1 on malformed input. Does not decode the
 * name text — callers that need the text use dns_decode_name(). */
static int dns_skip_name(const uint8_t *msg, int msg_len, int off) {
    int start = off;
    while (off < msg_len) {
        uint8_t len = msg[off];
        if (len == 0) { off++; return off - start; }
        if ((len & 0xC0) == 0xC0) { /* compression pointer: 2 bytes total */
            if (off + 2 > msg_len) return -1;
            off += 2;
            return off - start;
        }
        if (len > DNS_MAX_LABEL) return -1;
        off += 1 + len;
    }
    return -1;
}

/* ── Build and send the query, return the ephemeral-port socket
 * state implicitly via net_send/net_recv (no real socket layer
 * exists yet — same simplification unm.c's DHCP code already uses:
 * one outstanding "connection" at a time, matched by transaction id
 * embedded in the DNS header rather than by port). */
static int dns_send_query(const uint8_t server_ip[4], const char *host,
                          uint16_t txid) {
    static uint8_t frame[14 + 20 + 8 + 12 + DNS_MAX_NAME + 4];
    kmemset(frame, 0, sizeof(frame));

    eth_header_t *eth = (eth_header_t *)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF; /* see DHCP comment: no ARP
                                                       * cache yet, broadcast
                                                       * works for same-segment
                                                       * delivery under QEMU
                                                       * SLIRP and flat test
                                                       * networks */
    for (int i = 0; i < 6; i++) eth->src[i] = net.mac[i];
    eth->ethertype = net_bswap16(0x0800);

    ip4_hdr_t *ip = (ip4_hdr_t *)(frame + 14);
    ip->ver_ihl   = 0x45;
    ip->ttl       = 64;
    ip->protocol  = 17; /* UDP */
    for (int i = 0; i < 4; i++) ip->src_ip[i] = net.ip[i];
    for (int i = 0; i < 4; i++) ip->dst_ip[i] = server_ip[i];

    udp_hdr_t *udp = (udp_hdr_t *)(frame + 14 + 20);
    udp->src_port = net_bswap16(DNS_CLIENT_PORT);
    udp->dst_port = net_bswap16(DNS_PORT);

    dns_hdr_t *dh = (dns_hdr_t *)(frame + 14 + 20 + 8);
    dh->id      = net_bswap16(txid);
    dh->flags   = net_bswap16(DNS_FLAG_RD);
    dh->qdcount = net_bswap16(1);

    uint8_t *qname = frame + 14 + 20 + 8 + (int)sizeof(dns_hdr_t);
    int name_len = dns_encode_name(host, qname,
        (int)sizeof(frame) - (14 + 20 + 8 + (int)sizeof(dns_hdr_t) + 4));
    if (name_len < 0) return -1;

    uint8_t *qtail = qname + name_len;
    qtail[0] = 0x00; qtail[1] = DNS_QTYPE_A;   /* QTYPE  = A     (16-bit) */
    qtail[2] = 0x00; qtail[3] = DNS_QCLASS_IN; /* QCLASS = IN    (16-bit) */

    int dns_len = (int)sizeof(dns_hdr_t) + name_len + 4;
    int udp_len = 8 + dns_len;
    udp->length   = net_bswap16((uint16_t)udp_len);
    udp->checksum = 0; /* optional for IPv4 UDP per RFC 768 */

    int ip_len = 20 + udp_len;
    ip->total_len = net_bswap16((uint16_t)ip_len);
    ip->checksum  = net_ip_checksum(ip, 20);

    int frame_len = 14 + ip_len;
    return net_send(frame, (uint16_t)frame_len);
}

/* ── Parse the response, extract the first A record ──────────── */
static int dns_parse_response(const uint8_t *buf, int len, uint16_t txid,
                              uint8_t out_ip[4]) {
    if (len < 14 + 20 + 8 + (int)sizeof(dns_hdr_t)) return -1;

    const eth_header_t *eth = (const eth_header_t *)buf;
    if (net_bswap16(eth->ethertype) != 0x0800) return -1;

    const ip4_hdr_t *ip = (const ip4_hdr_t *)(buf + 14);
    if ((ip->ver_ihl >> 4) != 4 || ip->protocol != 17) return -1;
    int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || 14 + ihl + 8 > len) return -1;

    const udp_hdr_t *udp = (const udp_hdr_t *)(buf + 14 + ihl);
    if (net_bswap16(udp->src_port) != DNS_PORT) return -1;

    int dns_off = 14 + ihl + 8;
    if (dns_off + (int)sizeof(dns_hdr_t) > len) return -1;

    const dns_hdr_t *dh = (const dns_hdr_t *)(buf + dns_off);
    if (net_bswap16(dh->id) != txid) return -1;
    if (!(net_bswap16(dh->flags) & DNS_FLAG_QR)) return -1; /* not a response */

    uint16_t rcode = net_bswap16(dh->flags) & DNS_RCODE_MASK;
    if (rcode != 0) return -1; /* NXDOMAIN / SERVFAIL / etc */

    uint16_t qdcount = net_bswap16(dh->qdcount);
    uint16_t ancount = net_bswap16(dh->ancount);
    if (ancount == 0) return -1;

    int off = dns_off + (int)sizeof(dns_hdr_t);

    /* Skip the question section we echoed back */
    for (int i = 0; i < qdcount; i++) {
        int n = dns_skip_name(buf, len, off);
        if (n < 0) return -1;
        if (n > len - off - 4) return -1;
        off += n + 4; /* +QTYPE +QCLASS */
    }

    /* Walk answer records looking for the first A record */
    for (int i = 0; i < ancount; i++) {
        int n = dns_skip_name(buf, len, off);
        if (n < 0) return -1;
        off += n;

        if (off + 10 > len) return -1;
        uint16_t rtype  = net_bswap16(*(const uint16_t *)(buf + off));
        uint16_t rclass = net_bswap16(*(const uint16_t *)(buf + off + 2));
        uint16_t rdlen  = net_bswap16(*(const uint16_t *)(buf + off + 8));
        off += 10; /* TYPE(2) CLASS(2) TTL(4) RDLENGTH(2) */

        if (off + rdlen > len) return -1;

        if (rtype == DNS_QTYPE_A && rclass == DNS_QCLASS_IN && rdlen == 4) {
            for (int j = 0; j < 4; j++) out_ip[j] = buf[off + j];
            return 0;
        }
        off += rdlen;
    }
    return -1; /* no A record found (maybe only AAAA/CNAME) */
}

int dns_resolve(const char *hostname, const uint8_t server_ip[4],
               uint8_t out_ip[4]) {
    if (!net.initialized) return -1;
    if (!hostname || !hostname[0]) return -1;

    /* Already a dotted-quad? Skip the network round-trip entirely. */
    int dots = 0, digits_only = 1;
    for (const char *p = hostname; *p; p++) {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') digits_only = 0;
    }
    if (dots == 3 && digits_only) {
        int seg = 0, val = 0;
        for (const char *p = hostname; ; p++) {
            if (*p == '.' || *p == 0) {
                if (seg >= 4 || val > 255) return -1;
                out_ip[seg++] = (uint8_t)val;
                val = 0;
                if (*p == 0) break;
            } else if (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
            } else return -1;
        }
        return (seg == 4) ? 0 : -1;
    }

    uint16_t txid = (uint16_t)(pit_get_ticks() & 0xFFFF);
    if (dns_send_query(server_ip, hostname, txid) < 0) return -1;

    static uint8_t rxbuf[1600];
    uint32_t t0 = pit_get_ticks();
    while ((int)(pit_get_ticks() - t0) < 300) { /* 3s timeout */
        int n = net_recv(rxbuf, sizeof(rxbuf));
        if (n > 0 && dns_parse_response(rxbuf, n, txid, out_ip) == 0)
            return 0;
        pit_sleep(2);
    }
    return -1; /* timeout or NXDOMAIN */
}
