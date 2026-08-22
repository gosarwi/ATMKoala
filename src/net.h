#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

/* ── RTL8139 register offsets ─────────────────────────────── */
#define RTL_MAC0        0x00   /* 6 bytes: MAC address */
#define RTL_MAR0        0x08   /* Multicast filter */
#define RTL_TSD0        0x10   /* TX status (4 regs, stride 4) */
#define RTL_TSAD0       0x20   /* TX start address */
#define RTL_RBSTART     0x30   /* RX buffer start */
#define RTL_ERBCR       0x34   /* Early RX byte count */
#define RTL_ERSR        0x36   /* Early RX status */
#define RTL_CR          0x37   /* Command register */
#define RTL_CAPR        0x38   /* Current address of packet read */
#define RTL_IMR         0x3C   /* Interrupt mask */
#define RTL_ISR         0x3E   /* Interrupt status */
#define RTL_TCR         0x40   /* TX config */
#define RTL_RCR         0x44   /* RX config */
#define RTL_TCTR        0x48   /* Timer count */
#define RTL_MPC         0x4C   /* Missed packet counter */
#define RTL_CONFIG1     0x52
#define RTL_BMCR        0x62   /* Basic mode control */

/* CR bits */
#define RTL_CR_RESET    0x10
#define RTL_CR_RE       0x08   /* Receiver enable */
#define RTL_CR_TE       0x04   /* Transmitter enable */
#define RTL_CR_BUFE     0x01   /* Buffer empty */

/* ISR/IMR bits */
#define RTL_INT_ROK     0x0001 /* RX OK */
#define RTL_INT_RER     0x0002 /* RX error */
#define RTL_INT_TOK     0x0004 /* TX OK */
#define RTL_INT_TER     0x0008 /* TX error */
#define RTL_INT_RXOVW   0x0010 /* RX buffer overflow */

/* RCR bits */
#define RTL_RCR_AAP     (1<<0) /* Accept all packets */
#define RTL_RCR_APM     (1<<1) /* Accept physical match */
#define RTL_RCR_AM      (1<<2) /* Accept multicast */
#define RTL_RCR_AB      (1<<3) /* Accept broadcast */
#define RTL_RCR_WRAP    (1<<7) /* No FIFO overflow */
#define RTL_RCR_RBLEN8K (0<<11)
#define RTL_RCR_MXDMA   (7<<8) /* Max DMA burst */

/* TX descriptor */
#define RTL_TX_BUFS     4
#define RTL_TX_BUF_SIZE 1536
#define RTL_RX_RING_SIZE 8192
/* Ring plus RTL8139-required tail slack for a frame crossing its end. */
#define RTL_RX_BUF_SIZE  (RTL_RX_RING_SIZE + 16 + 1500)

/* Ethernet frame constants */
#define ETH_ALEN   6
#define ETH_HLEN   14
#define ETH_MAX    1514
#define ETH_MIN    60

/* Ethernet header */
typedef struct __attribute__((packed)) {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;
} eth_header_t;

/* ARP packet */
typedef struct __attribute__((packed)) {
    uint16_t htype;     /* 1 = Ethernet */
    uint16_t ptype;     /* 0x0800 = IPv4 */
    uint8_t  hlen;      /* 6 */
    uint8_t  plen;      /* 4 */
    uint16_t operation; /* 1=request, 2=reply */
    uint8_t  sha[6];    /* sender MAC */
    uint8_t  spa[4];    /* sender IP */
    uint8_t  tha[6];    /* target MAC */
    uint8_t  tpa[4];    /* target IP */
} arp_packet_t;

/* Single-entry ARP cache for the initial IPv4 stack. The cache is only
 * populated after a validated ARP reply and is intentionally bounded. */
typedef struct {
    uint8_t ip[4];
    uint8_t mac[ETH_ALEN];
    uint32_t updated_ticks;
    int valid;
} net_arp_cache_t;

/* Network card state */
typedef struct {
    uint16_t  io_base;      /* PCI I/O base address */
    uint8_t   mac[ETH_ALEN];
    uint8_t  *rx_buf;
    uint8_t  *tx_bufs[RTL_TX_BUFS];
    int       tx_cur;       /* current TX descriptor */
    uint32_t  rx_read_ptr;
    int       initialized;
    uint64_t  rx_packets;
    uint64_t  tx_packets;
    uint64_t  rx_bytes;
    uint64_t  tx_bytes;
    uint32_t  ip[4];        /* our IP (0=not configured) */
    net_arp_cache_t arp;
} net_state_t;

extern net_state_t net;

/* PCI detection + RTL8139 init */
int  net_init(void);

/* Send a raw Ethernet frame */
int  net_send(const uint8_t *buf, uint16_t len);

/* Receive (non-blocking; returns len or 0) */
int  net_recv(uint8_t *buf, uint16_t maxlen);

/* IRQ handler (called from IDT) */
void net_irq_handler(void *regs);

/* Utility */
void net_print_info(void);
void net_print_stats(void);
/* Print detected PCI Ethernet controllers and their actual driver state. */
void net_print_drivers(void);
const char *net_mac_str(void);

/* ARP request (send "who has ip?") */
void net_arp_request(uint8_t target_ip[4]);
/* Consume a validated Ethernet frame; caches matching ARP replies. */
void net_arp_observe(const uint8_t *frame, uint16_t len);
int  net_arp_lookup(const uint8_t ip[4], uint8_t mac_out[ETH_ALEN]);
int  net_arp_selftest(void);

/* Pseudo-DHCP: assign static IP for demo */
void net_set_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/* ── Shared IPv4 / UDP headers ────────────────────────────────
 * Used by unm.c (DHCP), icmp.c (ping), and dns.c (resolver) so the
 * wire format is defined exactly once. */
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;       /* 0x45 = IPv4, 5 x 32-bit words header */
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;      /* 1=ICMP 6=TCP 17=UDP */
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} ip4_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* RFC 1071 Internet checksum — used for the IPv4 header (always)
 * and for ICMP/UDP payloads (when not left as 0, which IPv4 UDP
 * permits but ICMP does not — ping replies are commonly dropped or
 * flagged by stricter stacks if the checksum is missing). */
uint16_t net_ip_checksum(const void *data, int len);

/* Bounded raw IPv4/UDP exchange helpers for kernel protocols. This is not a
 * user socket ABI: callers serialize requests and validate their own replies. */
#define NET_UDP_PAYLOAD_MAX 512
int net_udp_sendto(const uint8_t dst_ip[4],uint16_t src_port,uint16_t dst_port,const void *payload,uint16_t len);
int net_udp_recvfrom(uint16_t dst_port,uint8_t src_ip[4],uint16_t *src_port,void *payload,uint16_t cap,uint16_t *out_len);
int net_udp_selftest(void);

/* Byte-swap helpers (host is little-endian x86-64, network order is
 * big-endian) — shared so every protocol module agrees on the name. */
static inline uint16_t net_bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
static inline uint32_t net_bswap32(uint32_t x) {
    return ((x & 0xFF) << 24) | (((x >> 8) & 0xFF) << 16) |
           (((x >> 16) & 0xFF) << 8) | ((x >> 24) & 0xFF);
}

#endif
