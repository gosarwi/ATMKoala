/*  unm.c — UserNet Manager for atmkoala
 *
 *  DHCP over QEMU SLIRP (UDP port 67/68).
 *  Static IP assignment.
 *  Profile storage in /uiu/etc/network.conf (INI-style via config.c).
 */

#include "unm.h"
#include "net.h"
#include "dns.h"
#include "config.h"
#include "util.h"
#include "vga.h"
#include "pit.h"
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

/* ── io helpers (same as in net.c) ─────────────────────────── */
static inline uint8_t  _inb(uint16_t p){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"dN"(p)); return v; }
static inline void     _outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"dN"(p)); }
static inline uint32_t _inl(uint16_t p){ uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"dN"(p)); return v; }
static inline void     _outl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"dN"(p)); }

/* ── Global state ───────────────────────────────────────────── */
unm_state_block_t g_unm = {0};

/* ── Utility ────────────────────────────────────────────────── */
void unm_ip_str(const uint8_t ip[4], char *out, int sz) {
    char tmp[8];
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        kuitoa(ip[i], tmp, 10);
        int l = (int)kstrlen(tmp);
        if (pos + l + 1 < sz) {
            kstrcpy(out + pos, tmp);
            pos += l;
            if (i < 3) { out[pos++] = '.'; }
        }
    }
    out[pos] = 0;
}

static void unm_ip_parse(const char *s, uint8_t out[4]) {
    int o = 0;
    uint32_t v = 0;
    for (; *s && o < 4; s++) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); }
        else if (*s == '.') { out[o++] = (uint8_t)(v & 0xFF); v = 0; }
    }
    if (o < 4) out[o] = (uint8_t)(v & 0xFF);
}

/* ── Console helpers ────────────────────────────────────────── */
static void unm_write(const char *s) {
    terminal_write(s);
}
static void unm_writeln(const char *s) {
    terminal_writeln(s);
}
static void unm_write_ip(const char *label, const uint8_t ip[4]) {
    char buf[24];
    unm_ip_str(ip, buf, sizeof(buf));
    terminal_write(label);
    terminal_writeln(buf);
}

/* ── DHCP packet structures ─────────────────────────────────── */
#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_ACK        5
#define DHCP_NACK       6

#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68

/* udp_hdr_t / ip4_hdr_t / net_ip_checksum() / net_bswap16/32() now
 * live in net.h / net.c, shared with icmp.c and dns.c. */
#define ip_checksum net_ip_checksum
#define bswap16     net_bswap16
#define bswap32     net_bswap32

/* BOOTP / DHCP payload */
typedef struct __attribute__((packed)) {
    uint8_t  op;         /* 1=BOOTREQUEST, 2=BOOTREPLY */
    uint8_t  htype;      /* 1=Ethernet */
    uint8_t  hlen;       /* 6 */
    uint8_t  hops;
    uint32_t xid;        /* transaction ID */
    uint16_t secs;
    uint16_t flags;      /* 0x8000 = broadcast */
    uint8_t  ciaddr[4];  /* client IP */
    uint8_t  yiaddr[4];  /* your (offered) IP */
    uint8_t  siaddr[4];  /* server IP */
    uint8_t  giaddr[4];  /* relay agent IP */
    uint8_t  chaddr[16]; /* client MAC */
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  magic[4];   /* 99.130.83.99 */
    uint8_t  options[308];
} dhcp_payload_t;

#define DHCP_MAGIC0  99
#define DHCP_MAGIC1  130
#define DHCP_MAGIC2  83
#define DHCP_MAGIC3  99

/* ── Build and send DHCP Discover / Request ─────────────────── */
static int unm_send_dhcp(int msg_type, uint32_t xid,
                          const uint8_t *server_ip, const uint8_t *req_ip) {

    /* Total frame: Eth(14) + IP(20) + UDP(8) + DHCP(~548) */
    static uint8_t frame[600];
    kmemset(frame, 0, sizeof(frame));

    /* ── Ethernet header ── */
    eth_header_t *eth = (eth_header_t *)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF; /* broadcast */
    for (int i = 0; i < 6; i++) eth->src[i] = net.mac[i];
    eth->ethertype = bswap16(0x0800); /* IPv4 */

    /* ── IP header ── */
    ip4_hdr_t *ip = (ip4_hdr_t *)(frame + 14);
    ip->ver_ihl   = 0x45;
    ip->tos       = 0;
    ip->id        = 0;
    ip->flags_frag= 0;
    ip->ttl       = 64;
    ip->protocol  = 17; /* UDP */
    for (int i = 0; i < 4; i++) ip->src_ip[i] = 0;
    for (int i = 0; i < 4; i++) ip->dst_ip[i] = 0xFF;

    /* ── UDP header ── */
    udp_hdr_t *udp = (udp_hdr_t *)(frame + 14 + 20);
    udp->src_port = bswap16(DHCP_CLIENT_PORT);
    udp->dst_port = bswap16(DHCP_SERVER_PORT);

    /* ── DHCP payload ── */
    dhcp_payload_t *dhcp = (dhcp_payload_t *)(frame + 14 + 20 + 8);
    dhcp->op    = 1; /* BOOTREQUEST */
    dhcp->htype = 1;
    dhcp->hlen  = 6;
    dhcp->hops  = 0;
    dhcp->xid   = xid;
    dhcp->secs  = 0;
    dhcp->flags = bswap16(0x8000); /* broadcast flag */
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = net.mac[i];
    dhcp->magic[0] = DHCP_MAGIC0; dhcp->magic[1] = DHCP_MAGIC1;
    dhcp->magic[2] = DHCP_MAGIC2; dhcp->magic[3] = DHCP_MAGIC3;

    /* Options */
    int opt = 0;
    dhcp->options[opt++] = 53; dhcp->options[opt++] = 1;
    dhcp->options[opt++] = (uint8_t)msg_type; /* DISCOVER or REQUEST */

    /* Param request list */
    dhcp->options[opt++] = 55; dhcp->options[opt++] = 4;
    dhcp->options[opt++] = 1;  /* subnet mask */
    dhcp->options[opt++] = 3;  /* router */
    dhcp->options[opt++] = 6;  /* DNS */
    dhcp->options[opt++] = 51; /* lease time */

    if (msg_type == DHCP_REQUEST) {
        if (req_ip) {
            dhcp->options[opt++] = 50; dhcp->options[opt++] = 4;
            for (int i = 0; i < 4; i++) dhcp->options[opt++] = req_ip[i];
        }
        if (server_ip) {
            dhcp->options[opt++] = 54; dhcp->options[opt++] = 4;
            for (int i = 0; i < 4; i++) dhcp->options[opt++] = server_ip[i];
        }
    }
    dhcp->options[opt++] = 255; /* END */

    int dhcp_len = (int)(sizeof(dhcp_payload_t));
    int udp_len  = 8 + dhcp_len;
    int ip_len   = 20 + udp_len;
    int frame_len = 14 + ip_len;

    ip->total_len = bswap16((uint16_t)ip_len);
    ip->checksum  = ip_checksum(ip, 20);

    udp->length   = bswap16((uint16_t)udp_len);
    udp->checksum = 0; /* optional for UDP */

    return net_send(frame, (uint16_t)frame_len);
}

/* ── Parse a received DHCP reply ────────────────────────────── */
static int unm_parse_dhcp_reply(const uint8_t *buf, int len,
                                  uint32_t xid, int want_type,
                                  uint8_t offered_ip[4],
                                  uint8_t server_ip[4],
                                  uint8_t gateway_ip[4],
                                  uint8_t dns_ip[4],
                                  uint32_t *lease_time) {
    if (len < 14 + 20 + 8 + (int)sizeof(dhcp_payload_t)) return -1;

    eth_header_t *eth = (eth_header_t *)buf;
    if (bswap16(eth->ethertype) != 0x0800) return -1;

    ip4_hdr_t *ip = (ip4_hdr_t *)(buf + 14);
    if (ip->protocol != 17) return -1;

    udp_hdr_t *udp = (udp_hdr_t *)(buf + 14 + 20);
    if (bswap16(udp->dst_port) != DHCP_CLIENT_PORT) return -1;

    dhcp_payload_t *dhcp = (dhcp_payload_t *)(buf + 14 + 20 + 8);
    if (dhcp->op != 2) return -1; /* BOOTREPLY */
    if (dhcp->xid != xid) return -1;

    /* Check magic */
    if (dhcp->magic[0] != DHCP_MAGIC0 || dhcp->magic[1] != DHCP_MAGIC1 ||
        dhcp->magic[2] != DHCP_MAGIC2 || dhcp->magic[3] != DHCP_MAGIC3) return -1;

    /* Extract offered IP */
    for (int i = 0; i < 4; i++) offered_ip[i] = dhcp->yiaddr[i];

    /* Parse options */
    int msg_type = 0;
    const uint8_t *opt = dhcp->options;
    const uint8_t *end = opt + sizeof(dhcp->options);

    while (opt < end && *opt != 255) {
        uint8_t code = *opt++;
        if (code == 0) continue; /* PAD */
        if (opt >= end) break;
        uint8_t olen = *opt++;
        if ((size_t)olen > (size_t)(end - opt)) break;
        if (code == 53 && olen == 1) {
            msg_type = *opt;
        } else if (code == 1 && olen == 4) {
            /* subnet — ignore for now */
        } else if (code == 3 && olen >= 4) {
            for (int i = 0; i < 4; i++) gateway_ip[i] = opt[i];
        } else if (code == 6 && olen >= 4) {
            for (int i = 0; i < 4; i++) dns_ip[i] = opt[i];
        } else if (code == 51 && olen == 4) {
            *lease_time = ((uint32_t)opt[0]<<24)|((uint32_t)opt[1]<<16)|
                          ((uint32_t)opt[2]<<8)|(uint32_t)opt[3];
        } else if (code == 54 && olen == 4) {
            for (int i = 0; i < 4; i++) server_ip[i] = opt[i];
        }
        opt += olen;
    }

    return (msg_type == want_type) ? 0 : -1;
}

/* ── DHCP Discover → Offer → Request → ACK ─────────────────── */
int unm_dhcp_request(void) {
    if (!net.initialized) {
        unm_writeln("  [UNM] No network card detected.");
        unm_writeln("  Start QEMU with: -netdev user,id=net0 -device rtl8139,netdev=net0");
        return -1;
    }

    unm_writeln("  [UNM] Sending DHCP Discover...");

    /* Transaction ID: use MAC-derived value */
    uint32_t xid = ((uint32_t)net.mac[2] << 24) | ((uint32_t)net.mac[3] << 16) |
                   ((uint32_t)net.mac[4] << 8)  | (uint32_t)net.mac[5];
    xid ^= 0xA5C3B1D0;

    g_unm.state = UNM_STATE_CONNECTING;

    /* Send Discover */
    if (unm_send_dhcp(DHCP_DISCOVER, xid, NULL, NULL) < 0) {
        unm_writeln("  [UNM] Failed to send DHCP Discover (TX error).");
        g_unm.state = UNM_STATE_FAILED;
        return -1;
    }

    /* Wait for Offer (~3 seconds, poll) */
    uint8_t offered[4] = {0};
    uint8_t server[4]  = {0};
    uint8_t gw[4]      = {0};
    uint8_t dns[4]     = {0};
    uint32_t lease     = 0;
    static uint8_t rxbuf[1600];

    unm_writeln("  [UNM] Waiting for DHCP Offer...");

    int got_offer = 0;
    uint32_t t0 = pit_get_ticks();
    while ((int)(pit_get_ticks() - t0) < 300) { /* 3 seconds at 100Hz */
        int n = net_recv(rxbuf, sizeof(rxbuf));
        if (n > 0) {
            if (unm_parse_dhcp_reply(rxbuf, n, xid, DHCP_OFFER,
                                      offered, server, gw, dns, &lease) == 0) {
                got_offer = 1;
                break;
            }
        }
        pit_sleep(2);
    }

    if (!got_offer) {
        /* QEMU SLIRP fallback: assign 10.0.2.15/24 gw=10.0.2.2 dns=10.0.2.3 */
        unm_writeln("  [UNM] No OFFER received — using QEMU SLIRP defaults.");
        offered[0] = 10; offered[1] = 0; offered[2] = 2; offered[3] = 15;
        server[0]  = 10; server[1]  = 0; server[2]  = 2; server[3]  = 2;
        gw[0]      = 10; gw[1]      = 0; gw[2]      = 2; gw[3]      = 2;
        dns[0]     = 10; dns[1]     = 0; dns[2]      = 2; dns[3]      = 3;
        lease = 86400;
    } else {
        char ipbuf[24];
        unm_ip_str(offered, ipbuf, sizeof(ipbuf));
        terminal_write("  [UNM] Offer received: ");
        terminal_writeln(ipbuf);

        /* Send Request */
        unm_writeln("  [UNM] Sending DHCP Request...");
        unm_send_dhcp(DHCP_REQUEST, xid, server, offered);

        /* Wait for ACK */
        int got_ack = 0;
        uint8_t ack_offered[4], ack_server[4], ack_gw[4], ack_dns[4];
        uint32_t ack_lease = 0;
        t0 = pit_get_ticks();
        while ((int)(pit_get_ticks() - t0) < 200) {
            int n = net_recv(rxbuf, sizeof(rxbuf));
            if (n > 0) {
                if (unm_parse_dhcp_reply(rxbuf, n, xid, DHCP_ACK,
                                          ack_offered, ack_server,
                                          ack_gw, ack_dns, &ack_lease) == 0) {
                    got_ack = 1;
                    for (int i = 0; i < 4; i++) offered[i] = ack_offered[i];
                    for (int i = 0; i < 4; i++) gw[i]      = ack_gw[i];
                    for (int i = 0; i < 4; i++) dns[i]     = ack_dns[i];
                    lease = ack_lease;
                    break;
                }
            }
            pit_sleep(2);
        }
        if (!got_ack) {
            unm_writeln("  [UNM] No ACK — using offered IP anyway.");
        } else {
            unm_writeln("  [UNM] DHCP ACK received.");
        }
    }

    /* Apply */
    net_set_ip(offered[0], offered[1], offered[2], offered[3]);
    for (int i = 0; i < 4; i++) g_unm.leased_ip[i]  = offered[i];
    for (int i = 0; i < 4; i++) g_unm.leased_gw[i]  = gw[i];
    for (int i = 0; i < 4; i++) g_unm.leased_dns[i] = dns[i];
    g_unm.lease_time = lease;
    g_unm.state = UNM_STATE_UP;

    char buf[24];
    unm_write_ip("  [UNM] IP      : ", offered);
    unm_write_ip("  [UNM] Gateway : ", gw);
    unm_write_ip("  [UNM] DNS     : ", dns);
    ksnprintf(buf, sizeof(buf), "  [UNM] Lease   : %u s", (unsigned)lease);
    unm_writeln(buf);
    unm_writeln("  [UNM] Connected.");
    return 0;
}

/* ── Static IP ──────────────────────────────────────────────── */
void unm_set_static(uint8_t ip[4], uint8_t nm[4],
                     uint8_t gw[4], uint8_t dns[4]) {
    if (!net.initialized) {
        unm_writeln("  [UNM] No network card detected.");
        g_unm.state = UNM_STATE_FAILED;
        return;
    }
    net_set_ip(ip[0], ip[1], ip[2], ip[3]);
    for (int i = 0; i < 4; i++) g_unm.leased_ip[i]  = ip[i];
    for (int i = 0; i < 4; i++) g_unm.leased_gw[i]  = gw[i];
    for (int i = 0; i < 4; i++) g_unm.leased_dns[i] = dns[i];
    g_unm.lease_time = 0;
    g_unm.state = UNM_STATE_UP;

    unm_write_ip("  [UNM] IP      : ", ip);
    unm_write_ip("  [UNM] Gateway : ", gw);
    unm_write_ip("  [UNM] DNS     : ", dns);
    unm_writeln("  [UNM] Static IP configured.");
}

/* ── Connect / disconnect ────────────────────────────────────── */
int unm_connect(void) {
    int idx = g_unm.active_profile;
    unm_mode_t mode = UNM_MODE_DHCP; /* default */
    unm_profile_t *prof = NULL;

    if (idx >= 0 && idx < g_unm.profile_count) {
        prof = &g_unm.profiles[idx];
        mode = prof->mode;
    }

    if (mode == UNM_MODE_STATIC && prof) {
        unm_set_static(prof->ip, prof->netmask, prof->gateway, prof->dns);
        return (g_unm.state == UNM_STATE_UP) ? 0 : -1;
    } else {
        return unm_dhcp_request();
    }
}

void unm_disconnect(void) {
    net_set_ip(0, 0, 0, 0);
    kmemset(g_unm.leased_ip,  0, 4);
    kmemset(g_unm.leased_gw,  0, 4);
    kmemset(g_unm.leased_dns, 0, 4);
    g_unm.state = UNM_STATE_DOWN;
    unm_writeln("  [UNM] Disconnected.");
}

/* ── Profile management ─────────────────────────────────────── */
int unm_profile_add(const char *name, unm_mode_t mode,
                     uint8_t ip[4], uint8_t nm[4],
                     uint8_t gw[4], uint8_t dns[4]) {
    if (g_unm.profile_count >= UNM_MAX_PROFILES) return -1;
    unm_profile_t *p = &g_unm.profiles[g_unm.profile_count];
    kstrcpy(p->name, name);
    p->mode = mode;
    if (ip)  for (int i=0;i<4;i++) p->ip[i]      = ip[i];
    if (nm)  for (int i=0;i<4;i++) p->netmask[i]  = nm[i];
    if (gw)  for (int i=0;i<4;i++) p->gateway[i]  = gw[i];
    if (dns) for (int i=0;i<4;i++) p->dns[i]      = dns[i];
    p->active = 0;
    return g_unm.profile_count++;
}

int unm_profile_set_active(int idx) {
    if (idx < 0 || idx >= g_unm.profile_count) return -1;
    for (int i = 0; i < g_unm.profile_count; i++)
        g_unm.profiles[i].active = 0;
    g_unm.profiles[idx].active = 1;
    g_unm.active_profile = idx;
    return 0;
}

void unm_profile_list(void) {
    if (g_unm.profile_count == 0) {
        unm_writeln("  (no profiles — run 'untui' to create one)");
        return;
    }
    for (int i = 0; i < g_unm.profile_count; i++) {
        unm_profile_t *p = &g_unm.profiles[i];
        char buf[80];
        char ipbuf[24] = "DHCP";
        if (p->mode == UNM_MODE_STATIC)
            unm_ip_str(p->ip, ipbuf, sizeof(ipbuf));
        ksnprintf(buf, sizeof(buf), "  [%d]%s %-18s  %-8s  %s",
            i, (i == g_unm.active_profile) ? "*" : " ",
            p->name,
            (p->mode == UNM_MODE_DHCP) ? "DHCP" : "Static",
            ipbuf);
        unm_writeln(buf);
    }
}

/* ── Status ─────────────────────────────────────────────────── */
void unm_status(void) {
    const char *states[] = { "DOWN", "CONNECTING", "UP", "FAILED" };
    char buf[80];
    ksnprintf(buf, sizeof(buf), "  State    : %s", states[g_unm.state]);
    unm_writeln(buf);

    if (!net.initialized) {
        unm_writeln("  Interface : no NIC detected");
        return;
    }
    terminal_write("  MAC      : "); unm_writeln(net_mac_str());

    if (g_unm.state == UNM_STATE_UP) {
        unm_write_ip("  IP       : ", g_unm.leased_ip);
        unm_write_ip("  Gateway  : ", g_unm.leased_gw);
        unm_write_ip("  DNS      : ", g_unm.leased_dns);
        if (g_unm.lease_time > 0) {
            ksnprintf(buf, sizeof(buf), "  Lease    : %u s", (unsigned)g_unm.lease_time);
            unm_writeln(buf);
        }
    } else {
        unm_writeln("  IP       : not configured");
    }

    if (g_unm.profile_count > 0) {
        int idx = g_unm.active_profile;
        if (idx >= 0 && idx < g_unm.profile_count) {
            ksnprintf(buf, sizeof(buf), "  Profile  : %s", g_unm.profiles[idx].name);
            unm_writeln(buf);
        }
    }
}

/* ── Save/load profiles ─────────────────────────────────────── */
void unm_save_profile(void) {
    /* We persist the active profile to g_netcfg (loaded by config.c) */
    if (g_unm.active_profile < 0 || g_unm.active_profile >= g_unm.profile_count) {
        /* Save current DHCP/static state as "default" profile */
        if (g_unm.state == UNM_STATE_UP) {
            char ipbuf[24];
            unm_ip_str(g_unm.leased_ip,  ipbuf, sizeof(ipbuf));
            cfg_set(&g_netcfg, "profile", "name", "default");
            cfg_set(&g_netcfg, "profile", "mode", "dhcp");
            cfg_set(&g_netcfg, "profile", "ip",   ipbuf);
        }
    } else {
        unm_profile_t *p = &g_unm.profiles[g_unm.active_profile];
        char ipbuf[24]; char gwbuf[24]; char dnsbuf[24];
        unm_ip_str(p->ip,      ipbuf,  sizeof(ipbuf));
        unm_ip_str(p->gateway, gwbuf,  sizeof(gwbuf));
        unm_ip_str(p->dns,     dnsbuf, sizeof(dnsbuf));
        cfg_set(&g_netcfg, "profile", "name", p->name);
        cfg_set(&g_netcfg, "profile", "mode",
            (p->mode == UNM_MODE_STATIC) ? "static" : "dhcp");
        cfg_set(&g_netcfg, "profile", "ip",  ipbuf);
        cfg_set(&g_netcfg, "profile", "gw",  gwbuf);
        cfg_set(&g_netcfg, "profile", "dns", dnsbuf);
    }
    cfg_save(&g_netcfg, "/uiu/etc/network.conf");
    unm_writeln("  [UNM] Profile saved.");
}

void unm_load_profile(void) {
    cfg_load(&g_netcfg, "/uiu/etc/network.conf");
    const char *name = cfg_get(&g_netcfg, "profile", "name");
    const char *mode = cfg_get(&g_netcfg, "profile", "mode");
    if (!name || !name[0]) return;

    unm_mode_t m = UNM_MODE_DHCP;
    if (mode && kstrcmp(mode, "static") == 0) m = UNM_MODE_STATIC;

    uint8_t ip[4]={0}, nm[4]={255,255,255,0}, gw[4]={0}, dns[4]={0};
    const char *s_ip  = cfg_get(&g_netcfg, "profile", "ip");
    const char *s_gw  = cfg_get(&g_netcfg, "profile", "gw");
    const char *s_dns = cfg_get(&g_netcfg, "profile", "dns");
    if (s_ip)  unm_ip_parse(s_ip,  ip);
    if (s_gw)  unm_ip_parse(s_gw,  gw);
    if (s_dns) unm_ip_parse(s_dns, dns);

    int idx = unm_profile_add(name, m, ip, nm, gw, dns);
    if (idx >= 0) unm_profile_set_active(idx);
}

/* ── Init ───────────────────────────────────────────────────── */
void unm_init(void) {
    kmemset(&g_unm, 0, sizeof(g_unm));
    g_unm.active_profile = -1;
    g_unm.state = UNM_STATE_DOWN;
    unm_load_profile();

    /* If no saved profile, create built-in DHCP profile */
    if (g_unm.profile_count == 0) {
        uint8_t z[4]={0}, nm[4]={255,255,255,0};
        unm_profile_add("DHCP (auto)", UNM_MODE_DHCP, z, nm, z, z);
        unm_profile_set_active(0);
    }
}

/* ── DNS stub ────────────────────────────────────────────────── */
int unm_dns_resolve(const char *hostname, uint8_t out_ip[4]) {
    /* Short-circuit names useful even with no network at all, or
     * under QEMU SLIRP where these map to its internal virtual
     * hosts rather than anything a real DNS query could resolve. */
    struct { const char *host; uint8_t ip[4]; } known[] = {
        { "localhost",   { 127, 0, 0, 1 } },
        { "gateway",     { 10, 0, 2, 2 } },
        { "router",      { 10, 0, 2, 2 } },
        { NULL, {0,0,0,0} }
    };
    for (int i = 0; known[i].host; i++) {
        if (kstrcmp(hostname, known[i].host) == 0) {
            for (int j = 0; j < 4; j++) out_ip[j] = known[i].ip[j];
            return 0;
        }
    }

    /* dns_resolve() itself recognises a dotted-quad and returns it
     * immediately without touching the network, so this single call
     * covers both "real hostname" and "literal IP" cases. */
    if (g_unm.state == UNM_STATE_UP && g_unm.leased_dns[0]) {
        if (dns_resolve(hostname, g_unm.leased_dns, out_ip) == 0)
            return 0;
    }

    /* No DNS server configured/reachable — last-resort dotted-quad
     * parse, so `unm dns 1.2.3.4` still works even fully offline. */
    int dots = 0;
    for (const char *p = hostname; *p; p++) if (*p == '.') dots++;
    if (dots == 3) {
        unm_ip_parse(hostname, out_ip);
        return 0;
    }
    return -1; /* unknown, and no DNS server available to ask */
}
