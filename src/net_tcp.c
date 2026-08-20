#include "net_tcp.h"
#include "net.h"
#include "pit.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

#define IP_PROTO_TCP 6
#define TCP_SYN 0x02
#define TCP_ACK 0x10
#define TCP_FIN 0x01
#define TCP_SYNACK (TCP_SYN|TCP_ACK)

typedef struct __attribute__((packed)) {
    uint16_t src_port,dst_port;
    uint32_t seq,ack;
    uint8_t data_off,flags;
    uint16_t window,checksum,urgent;
} tcp_hdr_t;
typedef struct __attribute__((packed)) {
    uint8_t src[4],dst[4],zero,proto;
    uint16_t len;
} tcp_pseudo_t;

#define ATM_TCP_MAX_PAYLOAD 512
static uint8_t tcp_csum_buf[sizeof(tcp_pseudo_t)+sizeof(tcp_hdr_t)+ATM_TCP_MAX_PAYLOAD];
static uint16_t tcp_checksum(const ip4_hdr_t *ip,const tcp_hdr_t *tcp,uint16_t len) {
    uint8_t *buf=tcp_csum_buf;
    if (len>sizeof(tcp_hdr_t)+ATM_TCP_MAX_PAYLOAD) return 0;
    tcp_pseudo_t *p=(tcp_pseudo_t *)buf;
    for(int i=0;i<4;i++){p->src[i]=ip->src_ip[i];p->dst[i]=ip->dst_ip[i];}
    p->zero=0; p->proto=IP_PROTO_TCP; p->len=net_bswap16(len);
    kmemcpy(buf+sizeof(*p),tcp,len);
    return net_ip_checksum(buf,(int)sizeof(*p)+len);
}

static int tcp_validate_frame(uint8_t *frame,int n,ip4_hdr_t **out_ip,tcp_hdr_t **out_tcp,int *out_ihl,int *out_len){
    if(!frame||n<ETH_HLEN+20+(int)sizeof(tcp_hdr_t))return -1;
    eth_header_t *eth=(eth_header_t*)frame;ip4_hdr_t *ip=(ip4_hdr_t*)(frame+ETH_HLEN);int ihl=(ip->ver_ihl&15)*4;
    if(net_bswap16(eth->ethertype)!=0x0800||(ip->ver_ihl>>4)!=4||ip->protocol!=IP_PROTO_TCP||ihl<20||ihl>60||n<ETH_HLEN+ihl+(int)sizeof(tcp_hdr_t)||net_ip_checksum(ip,ihl)!=0)return -1;
    int ip_len=net_bswap16(ip->total_len);if(ip_len<ihl+(int)sizeof(tcp_hdr_t)||ip_len>n-ETH_HLEN)return -1;
    tcp_hdr_t *tcp=(tcp_hdr_t*)(frame+ETH_HLEN+ihl);int thl=(tcp->data_off>>4)*4;
    if(thl<(int)sizeof(*tcp)||thl>60||ip_len<ihl+thl||tcp_checksum(ip,tcp,(uint16_t)(ip_len-ihl))!=0)return -1;
    if(out_ip)*out_ip=ip;if(out_tcp)*out_tcp=tcp;if(out_ihl)*out_ihl=ihl;if(out_len)*out_len=ip_len;return 0;
}

static int tcp_window_allows(const atm_tcp_conn_t *c,uint16_t n){ uint16_t lim=c?(c->peer_window<c->cwnd?c->peer_window:c->cwnd):0;return c&&n&&n<=ATM_TCP_MAX_PAYLOAD&&lim&&n<=lim; }
static void tcp_congestion_init(atm_tcp_conn_t *c){if(c){c->cwnd=ATM_TCP_MSS;c->ssthresh=ATM_TCP_MSS*4;}}
static void tcp_congestion_ack(atm_tcp_conn_t *c,uint16_t n){if(!c)return;if(c->cwnd<c->ssthresh){uint32_t v=c->cwnd+n;c->cwnd=v>ATM_TCP_MSS*4?ATM_TCP_MSS*4:(uint16_t)v;}}
static void tcp_congestion_loss(atm_tcp_conn_t *c){if(!c)return;c->ssthresh=c->cwnd>ATM_TCP_MSS?(uint16_t)(c->cwnd/2):ATM_TCP_MSS;c->cwnd=ATM_TCP_MSS;}
static int tcp_ooo_take(atm_tcp_conn_t *c,void *out,uint16_t cap){if(!c||!out)return -1;for(int i=0;i<ATM_TCP_OOO_MAX;i++)if(c->ooo[i].used&&c->ooo[i].seq==c->ack&&c->ooo[i].len<=cap){int n=c->ooo[i].len;kmemcpy(out,c->ooo[i].data,n);c->ack+=n;c->ooo[i].used=0;return n;}return 0;}
static void tcp_ooo_store(atm_tcp_conn_t *c,uint32_t seq,const void *data,uint16_t n){if(!c||!data||!n||n>ATM_TCP_MSS)return;for(int i=0;i<ATM_TCP_OOO_MAX;i++)if(c->ooo[i].used&&c->ooo[i].seq==seq)return;for(int i=0;i<ATM_TCP_OOO_MAX;i++)if(!c->ooo[i].used){c->ooo[i].seq=seq;c->ooo[i].len=n;kmemcpy(c->ooo[i].data,data,n);c->ooo[i].used=1;return;}}

static int tcp_send_control(atm_tcp_conn_t *c,uint8_t flags) {
    uint8_t frame[ETH_HLEN+20+sizeof(tcp_hdr_t)], mac[ETH_ALEN];
    if (!c || net_arp_lookup(c->remote_ip,mac)<0) return -1;
    kmemset(frame,0,sizeof(frame));
    eth_header_t *eth=(eth_header_t *)frame;
    for(int i=0;i<ETH_ALEN;i++){eth->dst[i]=mac[i];eth->src[i]=net.mac[i];}
    eth->ethertype=net_bswap16(0x0800);
    ip4_hdr_t *ip=(ip4_hdr_t *)(frame+ETH_HLEN);
    ip->ver_ihl=0x45; ip->ttl=64; ip->protocol=IP_PROTO_TCP;
    ip->total_len=net_bswap16(20+sizeof(tcp_hdr_t));
    for(int i=0;i<4;i++){ip->src_ip[i]=(uint8_t)net.ip[i];ip->dst_ip[i]=c->remote_ip[i];}
    ip->checksum=net_ip_checksum(ip,20);
    tcp_hdr_t *tcp=(tcp_hdr_t *)(frame+ETH_HLEN+20);
    tcp->src_port=net_bswap16(c->local_port); tcp->dst_port=net_bswap16(c->remote_port);
    tcp->seq=net_bswap32(c->seq); tcp->ack=net_bswap32(c->ack);
    tcp->data_off=0x50; tcp->flags=flags; tcp->window=net_bswap16(c->local_window?c->local_window:ATM_TCP_MAX_PAYLOAD);
    tcp->checksum=tcp_checksum(ip,tcp,sizeof(*tcp));
    return net_send(frame,sizeof(frame));
}

int atm_tcp_listen(atm_tcp_conn_t *l,uint16_t port){
    if(!l||!port||!net.initialized)return -1;kmemset(l,0,sizeof(*l));l->local_port=port;l->local_window=ATM_TCP_MAX_PAYLOAD;tcp_congestion_init(l);l->state=ATM_TCP_LISTEN;return 0;
}
int atm_tcp_accept(atm_tcp_conn_t *l,atm_tcp_conn_t *out,uint32_t timeout){
    uint8_t frame[ETH_MAX];if(!l||!out||l->state!=ATM_TCP_LISTEN)return -1;uint32_t start=pit_get_ticks();
    while((uint32_t)(pit_get_ticks()-start)<timeout){
        int n=net_recv(frame,sizeof(frame));if(n<ETH_HLEN+20+(int)sizeof(tcp_hdr_t)){pit_sleep(1);continue;}
        ip4_hdr_t *ip;tcp_hdr_t *t;int ihl,ip_len;
        if(tcp_validate_frame(frame,n,&ip,&t,&ihl,&ip_len)<0)continue;
        if(t->flags!=TCP_SYN||net_bswap16(t->dst_port)!=l->local_port)continue;
        kmemset(out,0,sizeof(*out));out->local_port=l->local_port;out->remote_port=net_bswap16(t->src_port);for(int i=0;i<4;i++)out->remote_ip[i]=ip->src_ip[i];out->seq=pit_get_ticks()^((uint32_t)out->local_port<<16);out->ack=net_bswap32(t->seq)+1;out->peer_window=net_bswap16(t->window);out->local_window=ATM_TCP_MAX_PAYLOAD;tcp_congestion_init(out);out->state=ATM_TCP_SYN_RECEIVED;
        for(out->retry_count=0;out->retry_count<3;out->retry_count++){
            if(tcp_send_control(out,TCP_SYNACK)<0)break;uint32_t slice=timeout/3u+1u,wait=pit_get_ticks();
            while((uint32_t)(pit_get_ticks()-wait)<slice){
                int rn=net_recv(frame,sizeof(frame));if(rn<ETH_HLEN+20+(int)sizeof(tcp_hdr_t)){pit_sleep(1);continue;}
                ip4_hdr_t *ri;tcp_hdr_t *rt;int rihl,rip_len;
                if(tcp_validate_frame(frame,rn,&ri,&rt,&rihl,&rip_len)<0)continue;
                if((rt->flags&TCP_ACK)&&net_bswap16(rt->src_port)==out->remote_port&&net_bswap16(rt->dst_port)==out->local_port&&net_bswap32(rt->ack)==out->seq+1){out->seq++;out->state=ATM_TCP_ESTABLISHED;return 0;}
            }
        }
        out->state=ATM_TCP_ERROR;return -1;
    }
    return -1;
}

int atm_tcp_connect(atm_tcp_conn_t *c,const uint8_t ip[4],uint16_t port,uint32_t timeout) {
    static uint16_t next_port=49152; uint8_t frame[ETH_MAX];
    if (!c || !ip || !port || !net.initialized) return -1;
    kmemset(c,0,sizeof(*c)); for(int i=0;i<4;i++) c->remote_ip[i]=ip[i];
    c->remote_port=port; c->local_port=next_port++; c->seq=pit_get_ticks()^((uint32_t)c->local_port<<16);
    c->state=ATM_TCP_SYN_SENT;
    if (net_arp_lookup(ip,(uint8_t[ETH_ALEN]){0})<0) {
        net_arp_request(c->remote_ip);
        uint32_t arp_start=pit_get_ticks();
        while ((uint32_t)(pit_get_ticks()-arp_start)<timeout/3u+1u) {
            (void)net_recv(frame,sizeof(frame));
            if (net_arp_lookup(ip,(uint8_t[ETH_ALEN]){0})==0) break;
            pit_sleep(1);
        }
        if (net_arp_lookup(ip,(uint8_t[ETH_ALEN]){0})<0) { c->state=ATM_TCP_ERROR; return -1; }
    }
    uint32_t slice=timeout/3u+1u;
    for(c->retry_count=0;c->retry_count<3;c->retry_count++){
        if (tcp_send_control(c,TCP_SYN)<0) { c->state=ATM_TCP_ERROR; return -1; }
        uint32_t start=pit_get_ticks();
        while ((uint32_t)(pit_get_ticks()-start)<slice) {
            int n=net_recv(frame,sizeof(frame));
            if (n<ETH_HLEN+20+(int)sizeof(tcp_hdr_t)) { pit_sleep(1); continue; }
            ip4_hdr_t *h;tcp_hdr_t *t;int ihl,ip_len;
            if(tcp_validate_frame(frame,n,&h,&t,&ihl,&ip_len)<0) continue;
            if(net_bswap16(t->src_port)!=c->remote_port || net_bswap16(t->dst_port)!=c->local_port || t->flags!=TCP_SYNACK || net_bswap32(t->ack)!=c->seq+1) continue;
            c->ack=net_bswap32(t->seq)+1; c->seq++; c->peer_window=net_bswap16(t->window); c->local_window=ATM_TCP_MAX_PAYLOAD; tcp_congestion_init(c); c->state=ATM_TCP_ESTABLISHED;
            return tcp_send_control(c,TCP_ACK);
        }
    }
    c->state=ATM_TCP_ERROR; return -1;
}
int atm_tcp_send(atm_tcp_conn_t *c,const void *p,uint16_t n){
    uint8_t frame[ETH_HLEN+20+sizeof(tcp_hdr_t)+ATM_TCP_MAX_PAYLOAD],mac[ETH_ALEN];
    if(!c||!p||c->state!=ATM_TCP_ESTABLISHED||!tcp_window_allows(c,n)||net_arp_lookup(c->remote_ip,mac)<0)return -1;
    kmemset(frame,0,sizeof(frame)); eth_header_t *e=(eth_header_t*)frame;
    for(int i=0;i<ETH_ALEN;i++){e->dst[i]=mac[i];e->src[i]=net.mac[i];} e->ethertype=net_bswap16(0x0800);
    ip4_hdr_t *ip=(ip4_hdr_t*)(frame+ETH_HLEN); ip->ver_ihl=0x45;ip->ttl=64;ip->protocol=IP_PROTO_TCP;ip->total_len=net_bswap16(20+sizeof(tcp_hdr_t)+n);
    for(int i=0;i<4;i++){ip->src_ip[i]=(uint8_t)net.ip[i];ip->dst_ip[i]=c->remote_ip[i];}ip->checksum=net_ip_checksum(ip,20);
    tcp_hdr_t *t=(tcp_hdr_t*)(frame+ETH_HLEN+20);t->src_port=net_bswap16(c->local_port);t->dst_port=net_bswap16(c->remote_port);t->seq=net_bswap32(c->seq);t->ack=net_bswap32(c->ack);t->data_off=0x50;t->flags=TCP_ACK|0x08;t->window=net_bswap16(c->local_window?c->local_window:ATM_TCP_MAX_PAYLOAD);kmemcpy((uint8_t*)t+sizeof(*t),p,n);t->checksum=tcp_checksum(ip,t,(uint16_t)(sizeof(*t)+n));
    uint32_t expected=c->seq+n;
    for(c->retry_count=0;c->retry_count<3;c->retry_count++){
        if(net_send(frame,(uint16_t)(ETH_HLEN+20+sizeof(*t)+n))<0)return -1;
        uint8_t rx[ETH_MAX]; uint32_t start=pit_get_ticks();
        while((uint32_t)(pit_get_ticks()-start)<100u){
            int rn=net_recv(rx,sizeof(rx)); if(rn<ETH_HLEN+20+(int)sizeof(tcp_hdr_t)){pit_sleep(1);continue;}
            ip4_hdr_t *ri;tcp_hdr_t *rt;int ihl,ip_len;
            if(tcp_validate_frame(rx,rn,&ri,&rt,&ihl,&ip_len)<0)continue;
            if(net_bswap16(rt->src_port)==c->remote_port&&net_bswap16(rt->dst_port)==c->local_port&&(rt->flags&TCP_ACK)&&net_bswap32(rt->ack)==expected){c->peer_window=net_bswap16(rt->window);c->seq=expected;tcp_congestion_ack(c,n);return (int)n;}
        }
    }
    tcp_congestion_loss(c);c->state=ATM_TCP_ERROR;return -1;
}
int atm_tcp_recv(atm_tcp_conn_t *c,void *p,uint16_t cap,uint32_t timeout){
    uint8_t frame[ETH_MAX]; if(!c||!p||!cap||c->state!=ATM_TCP_ESTABLISHED)return -1;
    int queued=tcp_ooo_take(c,p,cap);if(queued>0){if(tcp_send_control(c,TCP_ACK)<0){c->state=ATM_TCP_ERROR;return -1;}return queued;}
    uint32_t start=pit_get_ticks();
    while((uint32_t)(pit_get_ticks()-start)<timeout){
        int n=net_recv(frame,sizeof(frame)); if(n<ETH_HLEN+20+(int)sizeof(tcp_hdr_t)){pit_sleep(1);continue;}
        ip4_hdr_t *ip;tcp_hdr_t *h;int ihl,ip_len;
        if(tcp_validate_frame(frame,n,&ip,&h,&ihl,&ip_len)<0)continue;
        int thl=(h->data_off>>4)*4;
        if(net_bswap16(h->src_port)!=c->remote_port||net_bswap16(h->dst_port)!=c->local_port)continue;
        int data_len=ip_len-ihl-thl; if(!data_len||data_len>c->local_window)continue;
        uint32_t seq=net_bswap32(h->seq);
        if(seq>c->ack){tcp_ooo_store(c,seq,(uint8_t*)h+thl,(uint16_t)data_len);c->peer_window=net_bswap16(h->window);(void)tcp_send_control(c,TCP_ACK);continue;}
        if(data_len>cap||seq!=c->ack)continue;
        kmemcpy(p,(uint8_t*)h+thl,(size_t)data_len);c->ack+=(uint32_t)data_len;c->peer_window=net_bswap16(h->window);
        if(tcp_send_control(c,TCP_ACK)<0){c->state=ATM_TCP_ERROR;return -1;}return data_len;
    }
    return -1;
}
int atm_tcp_selftest(void){
    uint8_t frame[ETH_HLEN+20+sizeof(tcp_hdr_t)];kmemset(frame,0,sizeof(frame));
    eth_header_t *e=(eth_header_t*)frame;e->ethertype=net_bswap16(0x0800);
    ip4_hdr_t *ip=(ip4_hdr_t*)(frame+ETH_HLEN);ip->ver_ihl=0x45;ip->ttl=64;ip->protocol=IP_PROTO_TCP;ip->total_len=net_bswap16(20+sizeof(tcp_hdr_t));ip->checksum=net_ip_checksum(ip,20);
    tcp_hdr_t *t=(tcp_hdr_t*)(frame+ETH_HLEN+20);t->src_port=net_bswap16(1000);t->dst_port=net_bswap16(2000);t->seq=net_bswap32(1);t->data_off=0x50;t->flags=TCP_ACK;t->window=net_bswap16(64);t->checksum=tcp_checksum(ip,t,sizeof(*t));
    if(tcp_validate_frame(frame,sizeof(frame),0,0,0,0)<0)return -1;frame[ETH_HLEN+10]^=1;if(tcp_validate_frame(frame,sizeof(frame),0,0,0,0)==0)return -1;
    atm_tcp_conn_t c;kmemset(&c,0,sizeof(c));tcp_congestion_init(&c);c.peer_window=8;if(!tcp_window_allows(&c,8)||tcp_window_allows(&c,9)||tcp_window_allows(&c,0))return -1;return 0;
}

int atm_tcp_close(atm_tcp_conn_t *c){
    if(!c)return -1;
    if(c->state==ATM_TCP_ESTABLISHED){
        c->state=ATM_TCP_FIN_WAIT;
        if(tcp_send_control(c,TCP_FIN|TCP_ACK)<0){c->state=ATM_TCP_ERROR;return -1;}
        c->seq++;
    }
    c->state=ATM_TCP_CLOSED;return 0;
}
