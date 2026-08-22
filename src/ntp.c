#include "ntp.h"
#include "net.h"
#include "unm.h"
#include "atm_time.h"
#include "pit.h"
#include "util.h"
#include <stdint.h>

#define NTP_PORT 123u
#define NTP_CLIENT_PORT 49555u
#define NTP_PACKET_SIZE 48u
#define NTP_UNIX_EPOCH 2208988800ULL
#define NTP_UNIX_MIN 946684800LL   /* 2000-01-01 */
#define NTP_UNIX_MAX 4102444800LL  /* 2100-01-01 */
#define NTP_TIMEOUT_TICKS 300u

static uint32_t ntp_be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void ntp_put_be32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

static int ntp_parse_reply(const uint8_t *packet,uint16_t len,int64_t *unix_seconds,uint8_t *stratum){
    if(!packet||len<NTP_PACKET_SIZE||!unix_seconds||!stratum)return -1;
    uint8_t li=packet[0]>>6,version=(packet[0]>>3)&7,mode=packet[0]&7,s=packet[1];
    if(li==3||version<3||version>4||(mode!=4&&mode!=5)||s==0||s>15)return -1;
    uint64_t seconds=ntp_be32(packet+40);
    if(seconds<NTP_UNIX_EPOCH)return -1;
    int64_t unix_time=(int64_t)(seconds-NTP_UNIX_EPOCH);
    if(unix_time<NTP_UNIX_MIN||unix_time>=NTP_UNIX_MAX)return -1;
    *unix_seconds=unix_time;*stratum=s;return 0;
}

int ntp_sync_once(const char *server,ntp_result_t *out){
    uint8_t ip[4],request[NTP_PACKET_SIZE],reply[NET_UDP_PAYLOAD_MAX],source[4];uint16_t source_port=0,reply_len=0;
    if(!server||!server[0]||!out)return -1;
    if(unm_dns_resolve(server,ip)<0)return -1;
    kmemset(request,0,sizeof(request));request[0]=0x23; /* LI=0, VN=4, mode=client */
    uint32_t started=pit_get_ticks();
    if(net_udp_sendto(ip,NTP_CLIENT_PORT,NTP_PORT,request,sizeof(request))<0)return -1;
    while((uint32_t)(pit_get_ticks()-started)<NTP_TIMEOUT_TICKS){
        int got=net_udp_recvfrom(NTP_CLIENT_PORT,source,&source_port,reply,sizeof(reply),&reply_len);
        if(got>0&&source_port==NTP_PORT&&!kmemcmp(source,ip,4)){
            int64_t unix_time;uint8_t stratum;
            if(ntp_parse_reply(reply,reply_len,&unix_time,&stratum)==0&&atm_realtime_set_unix(unix_time)==0){
                kmemcpy(out->server_ip,ip,4);out->unix_seconds=unix_time;out->stratum=stratum;out->roundtrip_ticks=(uint32_t)(pit_get_ticks()-started);return 0;
            }
            return -1;
        }
        pit_sleep(2);
    }
    return -1;
}

int ntp_selftest(void){
    uint8_t packet[NTP_PACKET_SIZE];int64_t unix_time=0;uint8_t stratum=0;
    kmemset(packet,0,sizeof(packet));packet[0]=0x24;packet[1]=2;ntp_put_be32(packet+40,(uint32_t)(NTP_UNIX_EPOCH+1767225600ULL));
    if(ntp_parse_reply(packet,sizeof(packet),&unix_time,&stratum)<0||unix_time!=1767225600LL||stratum!=2)return -1;
    packet[0]=0xE4;if(ntp_parse_reply(packet,sizeof(packet),&unix_time,&stratum)>=0)return -1;
    packet[0]=0x24;packet[1]=0;return ntp_parse_reply(packet,sizeof(packet),&unix_time,&stratum)<0?0:-1;
}
