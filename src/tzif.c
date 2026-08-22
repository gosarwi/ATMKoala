#include "tzif.h"
#include "vfs.h"
#include "catfs_vfs.h"
#include "util.h"
#include <stdint.h>

#define TZIF_FILE_MAX 8192u
#define TZIF_TRANS_MAX 64u
#define TZIF_TYPE_MAX 16u
#define TZIF_NAME_MAX 96u

typedef struct { int32_t offset; uint8_t isdst; } tzif_type_t;
typedef struct { int64_t at; uint8_t type; } tzif_transition_t;
typedef struct {
    char name[TZIF_NAME_MAX];
    tzif_type_t types[TZIF_TYPE_MAX];
    tzif_transition_t transitions[TZIF_TRANS_MAX];
    uint32_t type_count,transition_count;
    uint8_t initial_type,loaded;
} tzif_state_t;

static tzif_state_t active;

static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int32_t sbe32(const uint8_t *p){return (int32_t)be32(p);}
static int64_t sbe64(const uint8_t *p){uint64_t v=((uint64_t)be32(p)<<32)|be32(p+4);return (int64_t)v;}
static void put_be32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

static int leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
static int mdays(int y,int m){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return m>=1&&m<=12?d[m-1]+(m==2&&leap(y)):0;}
static int valid(const rtc_datetime_t *t){return t&&t->year>=1970&&t->year<=2099&&t->month>=1&&t->month<=12&&t->day>=1&&t->day<=mdays(t->year,t->month)&&t->hour>=0&&t->hour<24&&t->minute>=0&&t->minute<60&&t->second>=0&&t->second<60;}
static int64_t civil_seconds(const rtc_datetime_t *t){int64_t days=0;for(int y=1970;y<t->year;y++)days+=leap(y)?366:365;for(int m=1;m<t->month;m++)days+=mdays(t->year,m);return days*86400LL+(int64_t)t->hour*3600LL+(int64_t)t->minute*60LL+t->second;}
static void seconds_to_civil(int64_t seconds,rtc_datetime_t *out){int64_t days=seconds/86400LL,rem=seconds%86400LL;if(rem<0){rem+=86400LL;days--;}int year=1970;while(days>=((int64_t)(leap(year)?366:365))&&year<2100){days-=leap(year)?366:365;year++;}int month=1;while(month<12&&days>=mdays(year,month)){days-=mdays(year,month);month++;}out->year=year;out->month=month;out->day=(int)days+1;out->hour=(int)(rem/3600LL);out->minute=(int)((rem/60LL)%60LL);out->second=(int)(rem%60LL);}

static int safe_zone_name(const char *zone){if(!zone||!zone[0]||kstrlen(zone)>=TZIF_NAME_MAX)return 0;for(const char *p=zone;*p;p++){if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='-'||*p=='+'))return 0;if(p[0]=='.'&&p[1]=='.')return 0;}return 1;}
static int header_counts(const uint8_t *h,uint32_t *timecnt,uint32_t *typecnt,uint32_t *charcnt,uint32_t *leapcnt,uint32_t *stdcnt,uint32_t *gmtcnt){if(!h||h[0]!='T'||h[1]!='Z'||h[2]!='i'||h[3]!='f')return -1;*gmtcnt=be32(h+20);*stdcnt=be32(h+24);*leapcnt=be32(h+28);*timecnt=be32(h+32);*typecnt=be32(h+36);*charcnt=be32(h+40);if(!*typecnt||*typecnt>TZIF_TYPE_MAX||*timecnt>TZIF_TRANS_MAX||*charcnt>512||*leapcnt>32||*stdcnt>*typecnt||*gmtcnt>*typecnt)return -1;return 0;}
static int block_bytes(const uint8_t *h,int time_size,uint32_t *out){uint32_t tc,ty,cc,lc,sc,gc;if(header_counts(h,&tc,&ty,&cc,&lc,&sc,&gc)<0)return -1;uint64_t n=(uint64_t)tc*(uint32_t)time_size+tc+(uint64_t)ty*6+cc+(uint64_t)lc*((uint32_t)time_size+4)+sc+gc;if(n>TZIF_FILE_MAX)return -1;*out=(uint32_t)n;return 0;}
static int parse_block(tzif_state_t *state,const uint8_t *h,const uint8_t *body,uint32_t body_len,int time_size){uint32_t tc,ty,cc,lc,sc,gc;if(header_counts(h,&tc,&ty,&cc,&lc,&sc,&gc)<0)return -1;uint32_t need=0;if(block_bytes(h,time_size,&need)<0||need>body_len)return -1;const uint8_t *times=body,*indices=times+tc*(uint32_t)time_size,*infos=indices+tc,*abbr=infos+ty*6;for(uint32_t i=0;i<ty;i++){state->types[i].offset=sbe32(infos+i*6);state->types[i].isdst=infos[i*6+4]?1:0;if(infos[i*6+5]>=cc)return -1;}state->type_count=ty;state->transition_count=tc;state->initial_type=0;for(uint32_t i=0;i<ty;i++)if(!state->types[i].isdst){state->initial_type=(uint8_t)i;break;}for(uint32_t i=0;i<tc;i++){uint8_t index=indices[i];if(index>=ty)return -1;state->transitions[i].at=time_size==8?sbe64(times+i*8):(int64_t)sbe32(times+i*4);state->transitions[i].type=index;if(i&&state->transitions[i].at<state->transitions[i-1].at)return -1;}(void)abbr;return 0;}
static int parse_memory(tzif_state_t *state,const uint8_t *data,uint32_t len){if(!state||!data||len<44)return -1;uint32_t first=0;if(block_bytes(data,4,&first)<0||44u+first>len)return -1;uint8_t version=data[4];if((version=='2'||version=='3')&&44u+first+44u<=len&&data[44u+first]=='T'&&data[44u+first+1]=='Z'&&data[44u+first+2]=='i'&&data[44u+first+3]=='f'){const uint8_t *h=data+44u+first;uint32_t second=0;if(block_bytes(h,8,&second)<0||44u+first+44u+second>len)return -1;return parse_block(state,h,h+44,second,8);}return parse_block(state,data,data+44,first,4);}
static int tzif_path(const char *zone,char path[VFS_PATH_MAX]);

int tzif_load(const char *zone){uint8_t data[TZIF_FILE_MAX];char path[VFS_PATH_MAX];vfs_stat_t st;if(!catfs_vfs_is_mounted()||tzif_path(zone,path)<0)return -1;if(vfs_stat(path,&st)<0||!S_ISREG(st.st_mode)||st.st_size<44||st.st_size>TZIF_FILE_MAX)return -1;int fd=vfs_open(path,O_RDONLY,0);if(fd<0)return -1;int64_t n=vfs_read(fd,data,st.st_size);vfs_close(fd);if(n!=(int64_t)st.st_size)return -1;tzif_state_t next;kmemset(&next,0,sizeof(next));if(parse_memory(&next,data,(uint32_t)n)<0)return -1;kstrcpy(next.name,zone);next.loaded=1;active=next;return 0;}
int tzif_is_loaded(const char *zone){return active.loaded&&zone&&!kstrcmp(active.name,zone);}
const char *tzif_active_name(void){return active.loaded?active.name:"";}
void tzif_clear_active(void){kmemset(&active,0,sizeof(active));}
static int tzif_path(const char *zone,char path[VFS_PATH_MAX]){if(!safe_zone_name(zone))return -1;kstrcpy(path,"/data/uiu/tzif/");kstrcat(path,zone);return 0;}
int tzif_import(const char *source_path,const char *zone){
    uint8_t data[TZIF_FILE_MAX];char target[VFS_PATH_MAX];vfs_stat_t st;tzif_state_t parsed;
    if(!source_path||!catfs_vfs_is_mounted()||tzif_path(zone,target)<0||vfs_stat(source_path,&st)<0||!S_ISREG(st.st_mode)||st.st_size<44||st.st_size>TZIF_FILE_MAX)return -1;
    int in=vfs_open(source_path,O_RDONLY,0);if(in<0)return -1;int64_t n=vfs_read(in,data,st.st_size);vfs_close(in);if(n!=(int64_t)st.st_size)return -1;
    kmemset(&parsed,0,sizeof(parsed));if(parse_memory(&parsed,data,(uint32_t)n)<0)return -1;
    (void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/tzif",0755);int out=vfs_open(target,O_WRONLY|O_CREAT|O_EXCL,0644);if(out<0)return -1;int64_t wrote=vfs_write(out,data,(uint32_t)n);vfs_close(out);if(wrote!=n){(void)vfs_unlink(target);return -1;}return 0;
}
int tzif_remove(const char *zone){char path[VFS_PATH_MAX];if(!catfs_vfs_is_mounted()||tzif_path(zone,path)<0)return -1;if(tzif_is_loaded(zone))tzif_clear_active();return vfs_unlink(path);}
int tzif_convert(const char *zone,const rtc_datetime_t *utc,rtc_datetime_t *local,int *offset_minutes,int *dst_active){if(!valid(utc)||!local)return -1;if(!tzif_is_loaded(zone)&&tzif_load(zone)<0)return -1;int64_t now=civil_seconds(utc);uint8_t type=active.initial_type;for(uint32_t i=0;i<active.transition_count;i++){if(active.transitions[i].at>now)break;type=active.transitions[i].type;}int32_t offset=active.types[type].offset;if(offset%60)return -1;seconds_to_civil(now+offset,local);if(!valid(local))return -1;if(offset_minutes)*offset_minutes=offset/60;if(dst_active)*dst_active=active.types[type].isdst;return 0;}
int tzif_selftest(void){uint8_t raw[80];tzif_state_t test;rtc_datetime_t utc={1970,1,1,0,0,1},local;kmemset(raw,0,sizeof(raw));raw[0]='T';raw[1]='Z';raw[2]='i';raw[3]='f';put_be32(raw+32,1);put_be32(raw+36,2);put_be32(raw+40,8);put_be32(raw+44,0);raw[48]=1;put_be32(raw+49,0);raw[53]=0;raw[54]=0;put_be32(raw+55,3600);raw[59]=1;raw[60]=4;raw[61]='S';raw[62]='T';raw[63]='D';raw[64]=0;raw[65]='D';raw[66]='S';raw[67]='T';raw[68]=0;kmemset(&test,0,sizeof(test));if(parse_memory(&test,raw,69)<0)return -1;if(test.transition_count!=1||test.type_count!=2)return -6;if(test.transitions[0].type!=1)return -7;if(test.types[1].offset!=3600)return -8;if(test.transitions[0].at!=0)return -9;if(civil_seconds(&utc)!=1)return -10;kstrcpy(test.name,"Test/Zone");test.loaded=1;tzif_state_t saved=active;active=test;int off=0,dst=0;int rc=tzif_convert("Test/Zone",&utc,&local,&off,&dst);active=saved;if(rc<0)return -2;if(off!=60)return -3;if(!dst)return -4;if(local.hour!=1||local.minute!=0)return -5;return 0;}
