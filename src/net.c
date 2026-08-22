/*  net.c — RTL8139 NIC driver for atmkoala OS v0.5
 *
 *  Detects RTL8139 via PCI config space scan (vendor=0x10EC, dev=0x8139).
 *  Initializes TX/RX, handles basic Ethernet framing.
 *  Supports: raw send/recv, ARP request, static IP config.
 *
 *  QEMU emulates RTL8139 with: -netdev user,id=n0 -device rtl8139,netdev=n0
 */
#include "net.h"
#include "kmalloc.h"
#include "util.h"
#include "vga.h"
#include "idt.h"
#include "pit.h"
#include "unm.h"
#include "hw_y116.h"
#include <stdint.h>
#include <stddef.h>

net_state_t net = {0};

/* ── Port I/O helpers ─────────────────────────────────────── */
/* ── PCI config helpers ───────────────────────────────────── */
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = (1u<<31) | ((uint32_t)bus<<16) |
                    ((uint32_t)(dev&0x1F)<<11) |
                    ((uint32_t)(fn&0x07)<<8)   |
                    (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val) {
    uint32_t addr = (1u<<31) | ((uint32_t)bus<<16) |
                    ((uint32_t)(dev&0x1F)<<11) |
                    ((uint32_t)(fn&0x07)<<8)   |
                    (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

#define RTL_VENDOR  0x10EC
#define RTL_DEVID   0x8139

/* Use boot-time PCI inventory rather than assuming bus 0/function 0. This
 * discovers RTL8139 behind bridges and on multifunction devices too. */
static uint16_t pci_find_rtl8139(void) {
    for(int i=0;i<g_pci.count;i++){
        const pci_device_t *p=&g_pci.devs[i];
        if(p->vendor!=RTL_VENDOR||p->device!=RTL_DEVID)continue;
        uint32_t cmd=pci_read(p->bus,p->dev,p->fn,0x04);cmd|=0x05;pci_write(p->bus,p->dev,p->fn,0x04,cmd);
        uint32_t bar0=pci_read(p->bus,p->dev,p->fn,0x10);
        if((bar0&1u)&&(bar0&~0x3u))return (uint16_t)(bar0&~0x3u);
    }
    return 0;
}

/* ── RTL8139 init ─────────────────────────────────────────── */
int net_init(void) {
    uint16_t io = pci_find_rtl8139();
    if (!io) return -1;   /* no RTL8139 found */

    net.io_base = io;

    /* Power on */
    outb(io + RTL_CONFIG1, 0x00);

    /* Software reset */
    outb(io + RTL_CR, RTL_CR_RESET);
    int timeout = 10000;
    while ((inb(io + RTL_CR) & RTL_CR_RESET) && --timeout) {
        __asm__ volatile("pause");
    }
    if (!timeout) return -2;

    /* Read MAC */
    for (int i = 0; i < ETH_ALEN; i++)
        net.mac[i] = inb(io + RTL_MAC0 + i);

    /* Allocate RX ring buffer */
    net.rx_buf = (uint8_t *)kmalloc(RTL_RX_BUF_SIZE);
    if (!net.rx_buf) return -3;
    kmemset(net.rx_buf, 0, RTL_RX_BUF_SIZE);

    /* Allocate TX buffers */
    for (int i = 0; i < RTL_TX_BUFS; i++) {
        net.tx_bufs[i] = (uint8_t *)kmalloc(RTL_TX_BUF_SIZE);
        if (!net.tx_bufs[i]) return -4;
    }
    net.tx_cur = 0;
    net.rx_read_ptr = 0;

    /* Set RX buffer address */
    outl(io + RTL_RBSTART, (uint32_t)(uintptr_t)net.rx_buf);

    /* TX buffer addresses */
    for (int i = 0; i < RTL_TX_BUFS; i++)
        outl(io + RTL_TSAD0 + i*4, (uint32_t)(uintptr_t)net.tx_bufs[i]);

    /* Enable interrupts: ROK + TOK + RER + TER */
    outw(io + RTL_IMR, RTL_INT_ROK | RTL_INT_TOK |
                       RTL_INT_RER | RTL_INT_TER);

    /* RX config: accept broadcast + multicast + physical */
    outl(io + RTL_RCR,
         RTL_RCR_AB | RTL_RCR_AM | RTL_RCR_APM |
         RTL_RCR_MXDMA | RTL_RCR_WRAP | RTL_RCR_RBLEN8K);

    /* TX config */
    outl(io + RTL_TCR, 0x03000700);

    /* Enable RX + TX */
    outb(io + RTL_CR, RTL_CR_RE | RTL_CR_TE);

    /* Install IRQ handler (IRQ 11 → INT 43 after PIC remap) */
    irq_install_handler(11, (irq_handler_t)net_irq_handler);

    net.initialized = 1;
    return 0;
}

/* ── Send ─────────────────────────────────────────────────── */
int net_send(const uint8_t *buf, uint16_t len) {
    if (!net.initialized || !buf || len > RTL_TX_BUF_SIZE) return -1;
    uint16_t wire_len = len < ETH_MIN ? ETH_MIN : len;

    uint16_t io = net.io_base;
    int cur = net.tx_cur;

    /* Copy caller bytes, then zero-pad minimum Ethernet payload safely. */
    for (uint16_t i = 0; i < len; i++) net.tx_bufs[cur][i] = buf[i];
    for (uint16_t i = len; i < wire_len; i++) net.tx_bufs[cur][i] = 0;

    /* Wait for TX idle */
    int timeout = 100000;
    while (!(inl(io + RTL_TSD0 + cur*4) & (1<<13)) && --timeout)
        __asm__ volatile("pause");

    /* Set length and start TX */
    outl(io + RTL_TSD0 + cur*4, (uint32_t)wire_len);

    net.tx_cur = (cur + 1) & (RTL_TX_BUFS - 1);
    net.tx_packets++;
    net.tx_bytes += wire_len;
    return 0;
}

/* ── Receive (non-blocking poll) ─────────────────────────── */
int net_recv(uint8_t *buf, uint16_t maxlen) {
    if (!net.initialized) return 0;
    uint16_t io = net.io_base;

    /* Check if buffer empty */
    if (inb(io + RTL_CR) & RTL_CR_BUFE) return 0;

    /* Read packet from RX ring */
    uint32_t rptr = net.rx_read_ptr;
    uint8_t  status_lo = net.rx_buf[rptr];
    uint8_t  status_hi = net.rx_buf[rptr+1];
    uint16_t pkt_len = (uint16_t)(
        net.rx_buf[rptr+2] | ((uint16_t)net.rx_buf[rptr+3] << 8)
    );
    uint16_t status = (uint16_t)status_lo | ((uint16_t)status_hi << 8);
    /* RTL8139 length includes the trailing 4-byte Ethernet FCS. */
    int good = (status & 0x0001) && pkt_len >= 4 && pkt_len <= ETH_MAX + 4;

    if (!good || pkt_len - 4 > maxlen) {
        /* advance past bad packet */
        net.rx_read_ptr = (rptr + 4 + pkt_len + 3) & (RTL_RX_RING_SIZE-4);
        outw(io + RTL_CAPR, (uint16_t)(net.rx_read_ptr - 16));
        return 0;
    }

    /* Copy frame without hardware-provided FCS. */
    uint16_t frame_len = (uint16_t)(pkt_len - 4);
    for (uint16_t i = 0; i < frame_len; i++)
        buf[i] = net.rx_buf[rptr + 4 + i];
    net_arp_observe(buf,frame_len);

    /* Advance read pointer (aligned to 4 bytes) */
    net.rx_read_ptr = (rptr + 4 + pkt_len + 3) & (RTL_RX_RING_SIZE-4);
    outw(io + RTL_CAPR, (uint16_t)(net.rx_read_ptr - 16));

    net.rx_packets++;
    net.rx_bytes += frame_len;
    return (int)frame_len;
}

/* Bounded UDP is intentionally a kernel-protocol helper, not a socket
 * abstraction. It owns one net_recv poll at a time and drops unrelated frames. */
static int net_udp_parse_frame(const uint8_t *frame,uint16_t frame_len,uint16_t want_port,uint8_t src_ip[4],uint16_t *src_port,void *payload,uint16_t cap,uint16_t *out_len){
    if(!frame||frame_len<ETH_HLEN+20+8||!payload||!out_len)return -1;
    const eth_header_t *eth=(const eth_header_t *)frame;
    if(net_bswap16(eth->ethertype)!=0x0800)return -1;
    const ip4_hdr_t *ip=(const ip4_hdr_t *)(frame+ETH_HLEN);int ihl=(ip->ver_ihl&15)*4;
    if((ip->ver_ihl>>4)!=4||ihl<20||ip->protocol!=17||ETH_HLEN+ihl+8>frame_len)return -1;
    uint16_t ip_len=net_bswap16(ip->total_len);if(ip_len<(uint16_t)(ihl+8)||ETH_HLEN+ip_len>frame_len)return -1;
    const udp_hdr_t *udp=(const udp_hdr_t *)(frame+ETH_HLEN+ihl);uint16_t udp_len=net_bswap16(udp->length),body=(uint16_t)(udp_len-8);
    if(udp_len<8||udp_len>(uint16_t)(ip_len-ihl)||net_bswap16(udp->dst_port)!=want_port||body>cap)return -1;
    kmemcpy(payload,frame+ETH_HLEN+ihl+8,body);if(src_ip)kmemcpy(src_ip,ip->src_ip,4);if(src_port)*src_port=net_bswap16(udp->src_port);*out_len=body;return 0;
}
static int ip_nonzero(const uint8_t ip[4]){return ip&&((ip[0]|ip[1]|ip[2]|ip[3])!=0);}
static int ip_nonzero32(const uint32_t ip[4]){return ip&&((ip[0]|ip[1]|ip[2]|ip[3])!=0);}
static void net_udp_next_hop(const uint8_t dst[4],uint8_t out[4]){
    uint8_t mask[4]={255,255,255,0},gateway[4]={0,0,0,0};
    if(g_unm.active_profile>=0&&g_unm.active_profile<g_unm.profile_count){unm_profile_t *p=&g_unm.profiles[g_unm.active_profile];for(int i=0;i<4;i++){if(p->netmask[i])mask[i]=p->netmask[i];gateway[i]=p->gateway[i];}}
    if(ip_nonzero(g_unm.leased_gw))for(int i=0;i<4;i++)gateway[i]=g_unm.leased_gw[i];
    int local=1;for(int i=0;i<4;i++)if(((uint8_t)net.ip[i]&mask[i])!=(dst[i]&mask[i])){local=0;break;}
    for(int i=0;i<4;i++)out[i]=(!local&&ip_nonzero(gateway))?gateway[i]:dst[i];
}
static int net_udp_resolve_mac(const uint8_t target[4],uint8_t mac[ETH_ALEN]){
    if(net_arp_lookup(target,mac)==0)return 0;net_arp_request((uint8_t *)target);uint32_t started=pit_get_ticks();uint8_t frame[ETH_MAX];
    while((uint32_t)(pit_get_ticks()-started)<100u){(void)net_recv(frame,sizeof(frame));if(net_arp_lookup(target,mac)==0)return 0;pit_sleep(2);}return -1;
}
int net_udp_sendto(const uint8_t dst_ip[4],uint16_t src_port,uint16_t dst_port,const void *payload,uint16_t len){
    static uint8_t frame[ETH_HLEN+20+8+NET_UDP_PAYLOAD_MAX];static uint16_t packet_id=1;uint8_t next_hop[4],mac[ETH_ALEN];
    if(!net.initialized||!dst_ip||!payload||!src_port||!dst_port||len>NET_UDP_PAYLOAD_MAX||!ip_nonzero32(net.ip))return -1;
    net_udp_next_hop(dst_ip,next_hop);if(net_udp_resolve_mac(next_hop,mac)<0)return -1;
    kmemset(frame,0,sizeof(frame));eth_header_t *eth=(eth_header_t *)frame;for(int i=0;i<ETH_ALEN;i++){eth->dst[i]=mac[i];eth->src[i]=net.mac[i];}eth->ethertype=net_bswap16(0x0800);
    ip4_hdr_t *ip=(ip4_hdr_t *)(frame+ETH_HLEN);ip->ver_ihl=0x45;ip->ttl=64;ip->protocol=17;ip->id=net_bswap16(packet_id++);for(int i=0;i<4;i++){ip->src_ip[i]=(uint8_t)net.ip[i];ip->dst_ip[i]=dst_ip[i];}
    udp_hdr_t *udp=(udp_hdr_t *)(frame+ETH_HLEN+20);udp->src_port=net_bswap16(src_port);udp->dst_port=net_bswap16(dst_port);udp->length=net_bswap16((uint16_t)(8+len));udp->checksum=0;kmemcpy(frame+ETH_HLEN+20+8,payload,len);
    ip->total_len=net_bswap16((uint16_t)(20+8+len));ip->checksum=net_ip_checksum(ip,20);return net_send(frame,(uint16_t)(ETH_HLEN+20+8+len));
}
int net_udp_recvfrom(uint16_t dst_port,uint8_t src_ip[4],uint16_t *src_port,void *payload,uint16_t cap,uint16_t *out_len){uint8_t frame[ETH_MAX];int n=net_recv(frame,sizeof(frame));if(n<=0)return 0;return net_udp_parse_frame(frame,(uint16_t)n,dst_port,src_ip,src_port,payload,cap,out_len)==0?1:-1;}
int net_udp_selftest(void){
    uint8_t frame[ETH_HLEN+20+8+4],body[4]={0};uint16_t n=0,port=0;kmemset(frame,0,sizeof(frame));eth_header_t *eth=(eth_header_t *)frame;eth->ethertype=net_bswap16(0x0800);ip4_hdr_t *ip=(ip4_hdr_t *)(frame+ETH_HLEN);ip->ver_ihl=0x45;ip->protocol=17;ip->total_len=net_bswap16(32);ip->src_ip[0]=1;ip->src_ip[1]=2;ip->src_ip[2]=3;ip->src_ip[3]=4;udp_hdr_t *udp=(udp_hdr_t *)(frame+ETH_HLEN+20);udp->src_port=net_bswap16(123);udp->dst_port=net_bswap16(42424);udp->length=net_bswap16(12);frame[ETH_HLEN+28]=1;frame[ETH_HLEN+29]=2;frame[ETH_HLEN+30]=3;frame[ETH_HLEN+31]=4;
    if(net_udp_parse_frame(frame,sizeof(frame),42424,NULL,&port,body,sizeof(body),&n)<0||port!=123||n!=4||body[3]!=4)return -1;udp->length=net_bswap16(7);return net_udp_parse_frame(frame,sizeof(frame),42424,NULL,&port,body,sizeof(body),&n)<0?0:-1;
}

/* ── IRQ handler ──────────────────────────────────────────── */
void net_irq_handler(void *regs) {
    (void)regs;
    if (!net.initialized) return;
    uint16_t io = net.io_base;
    uint16_t isr = inw(io + RTL_ISR);
    /* Acknowledge */
    outw(io + RTL_ISR, isr);
    /* RX OK: packets handled via polling (net_recv) */
}

/* ── ARP request ──────────────────────────────────────────── */
void net_arp_request(uint8_t target_ip[4]) {
    uint8_t frame[ETH_HLEN + sizeof(arp_packet_t)];
    eth_header_t *eth = (eth_header_t *)frame;
    arp_packet_t *arp = (arp_packet_t *)(frame + ETH_HLEN);

    /* Broadcast destination */
    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dst[i] = 0xFF;
        eth->src[i] = net.mac[i];
    }
    eth->ethertype = 0x0608; /* 0x0806 big-endian swap */

    arp->htype     = 0x0100; /* Ethernet BE */
    arp->ptype     = 0x0008; /* IPv4 BE */
    arp->hlen      = 6;
    arp->plen      = 4;
    arp->operation = 0x0100; /* request BE */
    for (int i = 0; i < 6; i++) arp->sha[i] = net.mac[i];
    for (int i = 0; i < 4; i++) arp->spa[i] = net.ip[i];
    for (int i = 0; i < 6; i++) arp->tha[i] = 0;
    for (int i = 0; i < 4; i++) arp->tpa[i] = target_ip[i];

    net_send(frame, sizeof(frame));
}

void net_arp_observe(const uint8_t *frame,uint16_t len) {
    if (!frame || len < ETH_HLEN + sizeof(arp_packet_t)) return;
    const eth_header_t *eth=(const eth_header_t *)frame;
    const arp_packet_t *arp=(const arp_packet_t *)(frame+ETH_HLEN);
    if (net_bswap16(eth->ethertype)!=0x0806 || net_bswap16(arp->htype)!=1 ||
        net_bswap16(arp->ptype)!=0x0800 || arp->hlen!=ETH_ALEN || arp->plen!=4) return;
    uint16_t op=net_bswap16(arp->operation);
    for (int i=0;i<4;i++) if (arp->tpa[i]!=(uint8_t)net.ip[i]) return;
    if(op==2) for (int i=0;i<ETH_ALEN;i++) if (arp->tha[i]!=net.mac[i]) return;
    if(op!=1 && op!=2) return;
    for (int i=0;i<4;i++) net.arp.ip[i]=arp->spa[i];
    for (int i=0;i<ETH_ALEN;i++) net.arp.mac[i]=arp->sha[i];
    net.arp.updated_ticks=pit_get_ticks(); net.arp.valid=1;
    if(op==1){
        uint8_t reply[ETH_HLEN+sizeof(arp_packet_t)];kmemset(reply,0,sizeof(reply));
        eth_header_t *re=(eth_header_t*)reply;arp_packet_t *ra=(arp_packet_t*)(reply+ETH_HLEN);
        for(int i=0;i<ETH_ALEN;i++){re->dst[i]=arp->sha[i];re->src[i]=net.mac[i];ra->sha[i]=net.mac[i];ra->tha[i]=arp->sha[i];}
        re->ethertype=net_bswap16(0x0806);ra->htype=net_bswap16(1);ra->ptype=net_bswap16(0x0800);ra->hlen=ETH_ALEN;ra->plen=4;ra->operation=net_bswap16(2);
        for(int i=0;i<4;i++){ra->spa[i]=(uint8_t)net.ip[i];ra->tpa[i]=arp->spa[i];}(void)net_send(reply,sizeof(reply));
    }
}

int net_arp_lookup(const uint8_t ip[4],uint8_t mac_out[ETH_ALEN]) {
    if (!ip || !mac_out || !net.arp.valid ||
        (uint32_t)(pit_get_ticks()-net.arp.updated_ticks)>6000u) return -1;
    for (int i=0;i<4;i++) if (net.arp.ip[i]!=ip[i]) return -1;
    for (int i=0;i<ETH_ALEN;i++) mac_out[i]=net.arp.mac[i];
    return 0;
}

int net_arp_selftest(void) {
    net_state_t saved=net; uint8_t frame[ETH_HLEN+sizeof(arp_packet_t)], out[ETH_ALEN];
    kmemset(&net,0,sizeof(net));
    for(int i=0;i<ETH_ALEN;i++) net.mac[i]=(uint8_t)(0x10+i);
    net.ip[0]=10;net.ip[1]=0;net.ip[2]=2;net.ip[3]=15;
    kmemset(frame,0,sizeof(frame)); eth_header_t *e=(eth_header_t*)frame;
    arp_packet_t *a=(arp_packet_t*)(frame+ETH_HLEN); e->ethertype=net_bswap16(0x0806);
    a->htype=net_bswap16(1);a->ptype=net_bswap16(0x0800);a->hlen=ETH_ALEN;a->plen=4;a->operation=net_bswap16(2);
    for(int i=0;i<ETH_ALEN;i++){a->sha[i]=(uint8_t)(0xA0+i);a->tha[i]=net.mac[i];}
    a->spa[0]=10;a->spa[1]=0;a->spa[2]=2;a->spa[3]=2;
    for(int i=0;i<4;i++) a->tpa[i]=(uint8_t)net.ip[i];
    net_arp_observe(frame,sizeof(frame));
    int ok=net_arp_lookup(a->spa,out)==0;
    for(int i=0;i<ETH_ALEN;i++) if(out[i]!=a->sha[i]) ok=0;
    uint8_t wrong[4]={10,0,2,3}; if(net_arp_lookup(wrong,out)==0) ok=0;
    net=saved; return ok?0:-1;
}

/* ── Static IP ────────────────────────────────────────────── */
void net_set_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    net.ip[0] = a; net.ip[1] = b;
    net.ip[2] = c; net.ip[3] = d;
}

/* ── Info / stats ─────────────────────────────────────────── */
static char mac_buf[20];
const char *net_mac_str(void) {
    uint8_t *m = net.mac;
    /* format: XX:XX:XX:XX:XX:XX */
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        mac_buf[i*3]   = hex[m[i] >> 4];
        mac_buf[i*3+1] = hex[m[i] & 0xF];
        mac_buf[i*3+2] = (i < 5) ? ':' : 0;
    }
    mac_buf[17] = 0;
    return mac_buf;
}

void net_print_drivers(void){
    int found=0; char line[112];
    terminal_writeln("=== PCI Ethernet drivers ===");
    terminal_writeln("  rtl8139: operational driver (10EC:8139, PCI inventory scan)");
    for(int i=0;i<g_pci.count;i++){
        const pci_device_t *p=&g_pci.devs[i];
        if(p->class_code!=0x02)continue; found=1;
        const char *state="detected; no driver";
        if(p->vendor==RTL_VENDOR&&p->device==RTL_DEVID)state=net.initialized?"RTL8139 active as eth0":"RTL8139 detected; init failed";
        else if(p->vendor==0x10EC&&(p->device==0x8168||p->device==0x8161||p->device==0x8125))state="Realtek RTL8168/8125 detected; driver unavailable";
        else if(p->vendor==0x8086)state="Intel Ethernet detected; e1000/e1000e driver unavailable";
        else if(p->vendor==0x1AF4)state="virtio-net controller detected; driver unavailable";
        ksnprintf(line,sizeof(line),"  %02x:%02x.%u %04x:%04x  %s",p->bus,p->dev,p->fn,p->vendor,p->device,state);terminal_writeln(line);
    }
    if(!found)terminal_writeln("  No PCI Ethernet controller found.");
    terminal_writeln("  Wi-Fi and Bluetooth require separate MAC/HCI drivers; controller discovery alone is not connectivity.");
}

void net_print_info(void) {
    char buf[16];
    if (!net.initialized) {
        terminal_writeln("  eth0: No supported RTL8139 initialized");
        terminal_writeln("  Run 'net drivers' for PCI controller state.");
        terminal_writeln("  QEMU example: -netdev user,id=n0 -device rtl8139,netdev=n0");
        return;
    }
    terminal_write("  eth0:  MAC="); terminal_writeln(net_mac_str());
    terminal_write("  IO:    0x");
    kuitoa(net.io_base, buf, 16); terminal_writeln(buf);
    terminal_write("  IP:    ");
    if (net.ip[0]) {
        kuitoa(net.ip[0],buf,10); terminal_write(buf); terminal_write(".");
        kuitoa(net.ip[1],buf,10); terminal_write(buf); terminal_write(".");
        kuitoa(net.ip[2],buf,10); terminal_write(buf); terminal_write(".");
        kuitoa(net.ip[3],buf,10); terminal_writeln(buf);
    } else {
        terminal_writeln("not configured (use 'ifconfig eth0 <ip>')");
    }
}

void net_print_stats(void) {
    char buf[24];
    terminal_writeln("=== Network Statistics ===");
    terminal_write("  RX packets: "); kuitoa((uint32_t)net.rx_packets, buf, 10); terminal_writeln(buf);
    terminal_write("  TX packets: "); kuitoa((uint32_t)net.tx_packets, buf, 10); terminal_writeln(buf);
    terminal_write("  RX bytes:   "); kuitoa((uint32_t)net.rx_bytes,   buf, 10); terminal_writeln(buf);
    terminal_write("  TX bytes:   "); kuitoa((uint32_t)net.tx_bytes,   buf, 10); terminal_writeln(buf);
}

/* RFC 1071 Internet checksum — shared by unm.c (DHCP/IP header),
 * icmp.c (ping) and dns.c (UDP payload, optional but recommended). */
uint16_t net_ip_checksum(const void *data, int len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}
