#include "mp3.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t frame_size,rate_hz,bitrate_kbps,samples;
    uint8_t version,channels;
} mp3_frame_t;

static uint32_t be32(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static int parse_frame(const uint8_t *p,uint32_t left,mp3_frame_t *out){
    static const uint16_t v1_l3[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const uint16_t v2_l3[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    uint32_t h;uint8_t ver_bits,layer,br,sr,pad,mode;uint32_t version,rate,bitrate,framesz,samples;
    if(!p||!out||left<4)return -1;h=be32(p);
    if((h&0xFFE00000u)!=0xFFE00000u)return -1;
    ver_bits=(uint8_t)((h>>19)&3u);layer=(uint8_t)((h>>17)&3u);br=(uint8_t)((h>>12)&15u);sr=(uint8_t)((h>>10)&3u);pad=(uint8_t)((h>>9)&1u);mode=(uint8_t)((h>>6)&3u);
    if(ver_bits==1||layer!=1||!br||br==15||sr==3)return -1;
    version=ver_bits==3?1u:(ver_bits==2?2u:25u);
    if(version==1){static const uint32_t rates[3]={44100,48000,32000};rate=rates[sr];bitrate=v1_l3[br];samples=1152;framesz=(144u*bitrate*1000u)/rate+pad;}
    else {static const uint32_t rates2[3]={22050,24000,16000},rates25[3]={11025,12000,8000};rate=version==2?rates2[sr]:rates25[sr];bitrate=v2_l3[br];samples=576;framesz=(72u*bitrate*1000u)/rate+pad;}
    if(framesz<4||framesz>left)return -1;
    out->frame_size=framesz;out->rate_hz=rate;out->bitrate_kbps=bitrate;out->samples=samples;out->version=(uint8_t)version;out->channels=mode==3?1:2;
    return 0;
}

int atm_mp3_probe(const uint8_t *data,uint32_t size,atm_mp3_info_t *out){
    uint32_t start=0,limit,pos,count=0;mp3_frame_t first,cur;
    if(!data||!out||size<12)return -1;kmemset(out,0,sizeof(*out));
    if(data[0]=='I'&&data[1]=='D'&&data[2]=='3'){
        uint32_t tag;if((data[6]&0x80)||(data[7]&0x80)||(data[8]&0x80)||(data[9]&0x80))return -1;
        tag=((uint32_t)data[6]<<21)|((uint32_t)data[7]<<14)|((uint32_t)data[8]<<7)|data[9];
        start=10u+tag+((data[5]&0x10)?10u:0u);if(start>=size)return -1;out->has_id3v2=1;
    }
    limit=size<ATM_MP3_SCAN_MAX?size:ATM_MP3_SCAN_MAX;
    for(pos=start;pos+12<=limit;pos++){
        mp3_frame_t second,third;
        if(parse_frame(data+pos,limit-pos,&first)<0)continue;
        if(parse_frame(data+pos+first.frame_size,limit-(pos+first.frame_size),&second)<0)continue;
        if(second.version!=first.version||second.rate_hz!=first.rate_hz||second.channels!=first.channels)continue;
        if(parse_frame(data+pos+first.frame_size+second.frame_size,limit-(pos+first.frame_size+second.frame_size),&third)<0)continue;
        if(third.version!=first.version||third.rate_hz!=first.rate_hz||third.channels!=first.channels)continue;
        break;
    }
    if(pos+12>limit)return -1;
    out->first_frame_offset=pos;out->first_frame_size=first.frame_size;out->sample_rate_hz=first.rate_hz;out->bitrate_kbps=first.bitrate_kbps;out->samples_per_frame=first.samples;out->mpeg_version=first.version;out->channels=first.channels;
    while(pos+4<=limit&&parse_frame(data+pos,limit-pos,&cur)==0){
        if(cur.version!=first.version||cur.rate_hz!=first.rate_hz||cur.channels!=first.channels)break;
        if(cur.bitrate_kbps!=first.bitrate_kbps)out->vbr=1;
        count++;pos+=cur.frame_size;
    }
    if(count<3)return -1;out->frame_count=count;out->duration_ms_estimate=(uint32_t)(((uint64_t)count*first.samples*1000u)/first.rate_hz);
    return 0;
}

int atm_mp3_selftest(void){
    static uint8_t frames[1251];atm_mp3_info_t info;
    kmemset(frames,0,sizeof(frames));
    for(uint32_t off=0;off<sizeof(frames);off+=417u){frames[off]=0xFF;frames[off+1]=0xFB;frames[off+2]=0x90;frames[off+3]=0x00;}
    if(atm_mp3_probe(frames,sizeof(frames),&info)<0||info.frame_count!=3||info.sample_rate_hz!=44100||info.bitrate_kbps!=128||info.channels!=2||info.mpeg_version!=1)return -1;
    frames[417]=0;if(atm_mp3_probe(frames,sizeof(frames),&info)==0)return -1;
    return 0;
}
