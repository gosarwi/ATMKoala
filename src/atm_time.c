#include "atm_time.h"
#include "rtc.h"
#include "pit.h"
#include "atm_syscall.h"
#include "tzif.h"
#include "util.h"
#include <stdint.h>

/* A manually applied network correction is intentionally volatile. The CMOS
 * clock remains a firmware-owned source unless a future explicit writeback path
 * is implemented and separately verified. */
static int64_t realtime_correction_seconds;

static int leap_year(int year){
    return (year%4==0 && year%100!=0) || year%400==0;
}

static int64_t rtc_unix_seconds(const rtc_datetime_t *t){
    static const uint8_t month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int64_t days=0;
    for(int y=1970;y<t->year;y++) days+=leap_year(y)?366:365;
    for(int m=1;m<t->month;m++){
        days+=month_days[m-1];
        if(m==2 && leap_year(t->year)) days++;
    }
    days+=t->day-1;
    return days*86400LL+(int64_t)t->hour*3600LL+(int64_t)t->minute*60LL+t->second;
}

typedef enum { TZ_FIXED=0,TZ_EU,TZ_US,TZ_AU,TZ_NZ,TZ_CHILE,TZ_EGYPT,TZ_ISRAEL } tz_rule_t;
typedef struct { const char *name; int16_t standard_minutes,dst_minutes; uint8_t rule; } tz_entry_t;

/* A compact current-era civil-time table for installer presets. This is not a
 * complete TZif history: political changes still require a kernel update. */
static const tz_entry_t tz_table[]={
 {"UTC",0,0,TZ_FIXED},{"Europe/London",0,60,TZ_EU},{"Europe/Dublin",0,60,TZ_EU},{"Atlantic/Reykjavik",0,0,TZ_FIXED},{"Europe/Lisbon",0,60,TZ_EU},
 {"Europe/Madrid",60,120,TZ_EU},{"Europe/Paris",60,120,TZ_EU},{"Europe/Brussels",60,120,TZ_EU},{"Europe/Amsterdam",60,120,TZ_EU},{"Europe/Berlin",60,120,TZ_EU},{"Europe/Copenhagen",60,120,TZ_EU},{"Europe/Oslo",60,120,TZ_EU},{"Europe/Stockholm",60,120,TZ_EU},{"Europe/Rome",60,120,TZ_EU},{"Europe/Vienna",60,120,TZ_EU},{"Europe/Prague",60,120,TZ_EU},{"Europe/Warsaw",60,120,TZ_EU},{"Europe/Budapest",60,120,TZ_EU},
 {"Europe/Athens",120,180,TZ_EU},{"Europe/Bucharest",120,180,TZ_EU},{"Europe/Helsinki",120,180,TZ_EU},{"Europe/Kyiv",120,180,TZ_EU},{"Europe/Riga",120,180,TZ_EU},{"Europe/Vilnius",120,180,TZ_EU},{"Europe/Moscow",180,180,TZ_FIXED},{"Europe/Istanbul",180,180,TZ_FIXED},
 {"Africa/Casablanca",60,60,TZ_FIXED},{"Africa/Algiers",60,60,TZ_FIXED},{"Africa/Lagos",60,60,TZ_FIXED},{"Africa/Cairo",120,180,TZ_EGYPT},{"Africa/Johannesburg",120,120,TZ_FIXED},{"Africa/Nairobi",180,180,TZ_FIXED},{"Africa/Addis_Ababa",180,180,TZ_FIXED},
 {"Asia/Jerusalem",120,180,TZ_ISRAEL},{"Asia/Riyadh",180,180,TZ_FIXED},{"Asia/Dubai",240,240,TZ_FIXED},{"Asia/Tehran",210,210,TZ_FIXED},{"Asia/Kabul",270,270,TZ_FIXED},{"Asia/Karachi",300,300,TZ_FIXED},{"Asia/Tashkent",300,300,TZ_FIXED},{"Asia/Yekaterinburg",300,300,TZ_FIXED},{"Asia/Omsk",360,360,TZ_FIXED},{"Asia/Novosibirsk",420,420,TZ_FIXED},{"Asia/Krasnoyarsk",420,420,TZ_FIXED},{"Asia/Irkutsk",480,480,TZ_FIXED},{"Asia/Yakutsk",540,540,TZ_FIXED},{"Asia/Vladivostok",600,600,TZ_FIXED},{"Asia/Magadan",660,660,TZ_FIXED},{"Asia/Kamchatka",720,720,TZ_FIXED},
 {"Asia/Kolkata",330,330,TZ_FIXED},{"Asia/Kathmandu",345,345,TZ_FIXED},{"Asia/Colombo",330,330,TZ_FIXED},{"Asia/Dhaka",360,360,TZ_FIXED},{"Asia/Yangon",390,390,TZ_FIXED},{"Asia/Bangkok",420,420,TZ_FIXED},{"Asia/Ho_Chi_Minh",420,420,TZ_FIXED},{"Asia/Jakarta",420,420,TZ_FIXED},{"Asia/Kuala_Lumpur",480,480,TZ_FIXED},{"Asia/Singapore",480,480,TZ_FIXED},{"Asia/Manila",480,480,TZ_FIXED},{"Asia/Shanghai",480,480,TZ_FIXED},{"Asia/Hong_Kong",480,480,TZ_FIXED},{"Asia/Taipei",480,480,TZ_FIXED},{"Asia/Ulaanbaatar",480,480,TZ_FIXED},{"Asia/Seoul",540,540,TZ_FIXED},{"Asia/Tokyo",540,540,TZ_FIXED},
 {"Australia/Perth",480,480,TZ_FIXED},{"Australia/Darwin",570,570,TZ_FIXED},{"Australia/Adelaide",570,630,TZ_AU},{"Australia/Brisbane",600,600,TZ_FIXED},{"Australia/Sydney",600,660,TZ_AU},{"Australia/Hobart",600,660,TZ_AU},{"Pacific/Port_Moresby",600,600,TZ_FIXED},{"Pacific/Guam",600,600,TZ_FIXED},{"Pacific/Fiji",720,720,TZ_FIXED},{"Pacific/Auckland",720,780,TZ_NZ},{"Pacific/Honolulu",-600,-600,TZ_FIXED},{"Pacific/Pago_Pago",-660,-660,TZ_FIXED},
 {"America/St_Johns",-210,-150,TZ_US},{"America/Halifax",-240,-180,TZ_US},{"America/Toronto",-300,-240,TZ_US},{"America/New_York",-300,-240,TZ_US},{"America/Detroit",-300,-240,TZ_US},{"America/Indiana/Indianapolis",-300,-240,TZ_US},{"America/Chicago",-360,-300,TZ_US},{"America/Winnipeg",-360,-300,TZ_US},{"America/Mexico_City",-360,-360,TZ_FIXED},{"America/Guatemala",-360,-360,TZ_FIXED},{"America/Costa_Rica",-360,-360,TZ_FIXED},{"America/Panama",-300,-300,TZ_FIXED},{"America/Bogota",-300,-300,TZ_FIXED},{"America/Lima",-300,-300,TZ_FIXED},{"America/Caracas",-240,-240,TZ_FIXED},{"America/La_Paz",-240,-240,TZ_FIXED},{"America/Santiago",-240,-180,TZ_CHILE},{"America/Asuncion",-180,-180,TZ_FIXED},{"America/Montevideo",-180,-180,TZ_FIXED},{"America/Argentina/Buenos_Aires",-180,-180,TZ_FIXED},{"America/Sao_Paulo",-180,-180,TZ_FIXED},{"America/Phoenix",-420,-420,TZ_FIXED},{"America/Denver",-420,-360,TZ_US},{"America/Edmonton",-360,-360,TZ_FIXED},{"America/Los_Angeles",-480,-420,TZ_US},{"America/Vancouver",-480,-420,TZ_US},{"America/Anchorage",-540,-480,TZ_US}
};

static int month_days(int year,int month){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return month>=1&&month<=12?d[month-1]+(month==2&&leap_year(year)):0;}
static int valid_civil(const rtc_datetime_t *t){return t&&t->year>=1970&&t->year<=2099&&t->month>=1&&t->month<=12&&t->day>=1&&t->day<=month_days(t->year,t->month)&&t->hour>=0&&t->hour<24&&t->minute>=0&&t->minute<60&&t->second>=0&&t->second<60;}
static int64_t civil_seconds(int year,int month,int day,int hour,int minute,int second){rtc_datetime_t t;t.year=year;t.month=month;t.day=day;t.hour=hour;t.minute=minute;t.second=second;return rtc_unix_seconds(&t);}
static int civil_weekday(int year,int month,int day){return (int)((civil_seconds(year,month,day,0,0,0)/86400LL+4LL)%7LL);}
static int nth_weekday(int year,int month,int weekday,int nth){int day=1+((weekday-civil_weekday(year,month,1)+7)%7)+7*(nth-1);return day<=month_days(year,month)?day:0;}
static int last_weekday(int year,int month,int weekday){int day=month_days(year,month);return day-((civil_weekday(year,month,day)-weekday+7)%7);}
static const tz_entry_t *tz_find(const char *name){if(!name||!name[0])name="UTC";for(uint32_t i=0;i<sizeof(tz_table)/sizeof(tz_table[0]);i++)if(!kstrcmp(name,tz_table[i].name))return &tz_table[i];return NULL;}
static int between(int64_t now,int64_t begin,int64_t end){return now>=begin&&now<end;}
static int south_dst(int64_t now,int year,tz_rule_t rule,int std_min,int dst_min){for(int base=year-1;base<=year;base++){int start_day,end_day,start_month,end_month,start_hour,end_hour;if(rule==TZ_AU){start_month=10;start_day=nth_weekday(base,10,0,1);start_hour=2;end_month=4;end_day=nth_weekday(base+1,4,0,1);end_hour=3;}else if(rule==TZ_NZ){start_month=9;start_day=last_weekday(base,9,0);start_hour=2;end_month=4;end_day=nth_weekday(base+1,4,0,1);end_hour=3;}else{start_month=9;start_day=nth_weekday(base,9,0,1);start_hour=0;end_month=4;end_day=nth_weekday(base+1,4,0,1);end_hour=0;}int64_t begin=civil_seconds(base,start_month,start_day,start_hour,0,0)-(int64_t)std_min*60LL;int64_t end=civil_seconds(base+1,end_month,end_day,end_hour,0,0)-(int64_t)dst_min*60LL;if(between(now,begin,end))return 1;}return 0;}
static int timezone_dst(const tz_entry_t *z,const rtc_datetime_t *utc){
    int y=utc->year;int64_t now=rtc_unix_seconds(utc),begin,end;
    if(z->rule==TZ_FIXED)return 0;
    if(z->rule==TZ_EU){begin=civil_seconds(y,3,last_weekday(y,3,0),1,0,0);end=civil_seconds(y,10,last_weekday(y,10,0),1,0,0);return between(now,begin,end);}
    if(z->rule==TZ_US){begin=civil_seconds(y,3,nth_weekday(y,3,0,2),2,0,0)-(int64_t)z->standard_minutes*60LL;end=civil_seconds(y,11,nth_weekday(y,11,0,1),2,0,0)-(int64_t)z->dst_minutes*60LL;return between(now,begin,end);}
    if(z->rule==TZ_AU||z->rule==TZ_NZ||z->rule==TZ_CHILE)return south_dst(now,y,(tz_rule_t)z->rule,z->standard_minutes,z->dst_minutes);
    if(z->rule==TZ_EGYPT){begin=civil_seconds(y,4,last_weekday(y,4,5),0,0,0)-(int64_t)z->standard_minutes*60LL;end=civil_seconds(y,10,last_weekday(y,10,4),24,0,0)-(int64_t)z->dst_minutes*60LL;return between(now,begin,end);}
    /* Israel: Friday before the final March Sunday at 02:00 standard time,
     * through final October Sunday at 02:00 daylight time. */
    begin=civil_seconds(y,3,last_weekday(y,3,0)-2,2,0,0)-(int64_t)z->standard_minutes*60LL;end=civil_seconds(y,10,last_weekday(y,10,0),2,0,0)-(int64_t)z->dst_minutes*60LL;return between(now,begin,end);
}
static void seconds_to_civil(int64_t seconds,rtc_datetime_t *out){int64_t days=seconds/86400LL,rem=seconds%86400LL;if(rem<0){rem+=86400LL;days--;}int year=1970;while(days>=(leap_year(year)?366:365)){days-=leap_year(year)?366:365;year++;}int month=1;while(days>=month_days(year,month)){days-=month_days(year,month);month++;}out->year=year;out->month=month;out->day=(int)days+1;out->hour=(int)(rem/3600LL);out->minute=(int)((rem/60LL)%60LL);out->second=(int)(rem%60LL);}

int atm_timezone_supported(const char *zone){return tz_find(zone)!=NULL;}
uint32_t atm_timezone_count(void){return (uint32_t)(sizeof(tz_table)/sizeof(tz_table[0]));}
const char *atm_timezone_name(uint32_t index){return index<atm_timezone_count()?tz_table[index].name:NULL;}
int atm_timezone_convert(const char *zone,const rtc_datetime_t *utc,rtc_datetime_t *local,int *offset_minutes,int *dst_active){
    if(tzif_is_loaded(zone))return tzif_convert(zone,utc,local,offset_minutes,dst_active);
    if(!valid_civil(utc)||!local)return -ATM_EINVAL;const tz_entry_t *z=tz_find(zone);if(!z)return -ATM_ENOSYS;int dst=timezone_dst(z,utc),offset=dst?z->dst_minutes:z->standard_minutes;seconds_to_civil(rtc_unix_seconds(utc)+(int64_t)offset*60LL,local);if(offset_minutes)*offset_minutes=offset;if(dst_active)*dst_active=dst;return 0;
}
int atm_realtime_utc(rtc_datetime_t *out){rtc_datetime_t raw;if(!out||rtc_read_datetime(&raw)<0)return -ATM_ENOSYS;int64_t corrected=rtc_unix_seconds(&raw)+realtime_correction_seconds;seconds_to_civil(corrected,out);return valid_civil(out)?0:-ATM_EINVAL;}
int atm_realtime_set_unix(int64_t unix_seconds){rtc_datetime_t target,raw;seconds_to_civil(unix_seconds,&target);if(!valid_civil(&target)||rtc_read_datetime(&raw)<0)return -ATM_EINVAL;realtime_correction_seconds=unix_seconds-rtc_unix_seconds(&raw);return 0;}
int atm_realtime_correction_seconds(int64_t *out){if(!out)return -ATM_EINVAL;*out=realtime_correction_seconds;return 0;}
void atm_realtime_clear_correction(void){realtime_correction_seconds=0;}
int atm_local_datetime(const char *zone,rtc_datetime_t *local,int *offset_minutes,int *dst_active){rtc_datetime_t utc;if(atm_realtime_utc(&utc)<0)return -ATM_ENOSYS;return atm_timezone_convert(zone,&utc,local,offset_minutes,dst_active);}
int atm_timezone_selftest(void){
    rtc_datetime_t utc,local;int off,dst;
    if(atm_timezone_count()<100u||!atm_timezone_name(0)||kstrcmp(atm_timezone_name(0),"UTC")||atm_timezone_name(atm_timezone_count())!=NULL)return -1;
    utc=(rtc_datetime_t){2026,1,15,12,0,0};if(atm_timezone_convert("Asia/Yekaterinburg",&utc,&local,&off,&dst)<0||off!=300||dst||local.hour!=17||local.day!=15)return -1;
    if(atm_timezone_convert("America/New_York",&utc,&local,&off,&dst)<0||off!=-300||dst||local.hour!=7)return -1;
    utc=(rtc_datetime_t){2026,7,1,12,0,0};if(atm_timezone_convert("America/New_York",&utc,&local,&off,&dst)<0||off!=-240||!dst||local.hour!=8)return -1;
    utc=(rtc_datetime_t){2026,3,29,1,30,0};if(atm_timezone_convert("Europe/London",&utc,&local,&off,&dst)<0||off!=60||!dst||local.hour!=2)return -1;
    utc=(rtc_datetime_t){2026,4,4,15,30,0};if(atm_timezone_convert("Australia/Sydney",&utc,&local,&off,&dst)<0||off!=660||!dst)return -1;
    utc=(rtc_datetime_t){2026,4,4,16,30,0};if(atm_timezone_convert("Australia/Sydney",&utc,&local,&off,&dst)<0||off!=600||dst)return -1;
    utc=(rtc_datetime_t){2026,10,29,21,30,0};if(atm_timezone_convert("Africa/Cairo",&utc,&local,&off,&dst)<0||off!=120||dst)return -1;
    return atm_timezone_convert("Invalid/Zone",&utc,&local,&off,&dst)<0?0:-1;
}

int atm_clock_gettime(int clock_id,atm_timespec_t *out){
    if(!out) return -ATM_EFAULT;
    uint32_t ticks=pit_get_ticks();
    if(clock_id==ATM_CLOCK_MONOTONIC){
        out->tv_sec=(int64_t)(ticks/100u);
        out->tv_nsec=(int64_t)(ticks%100u)*10000000LL;
        return 0;
    }
    if(clock_id!=ATM_CLOCK_REALTIME) return -ATM_EINVAL;
    rtc_datetime_t rtc;
    if(atm_realtime_utc(&rtc)<0) return -ATM_ENOSYS;
    out->tv_sec=rtc_unix_seconds(&rtc);
    out->tv_nsec=(int64_t)(ticks%100u)*10000000LL;
    return 0;
}

int atm_clock_getres(int clock_id,atm_timespec_t *out){
    if(!out) return -ATM_EFAULT;
    if(clock_id!=ATM_CLOCK_REALTIME && clock_id!=ATM_CLOCK_MONOTONIC) return -ATM_EINVAL;
    out->tv_sec=0;
    out->tv_nsec=10000000LL;
    return 0;
}

int atm_gettimeofday(atm_timeval_t *out){
    atm_timespec_t ts;
    if(!out) return -ATM_EFAULT;
    int rc=atm_clock_gettime(ATM_CLOCK_REALTIME,&ts);
    if(rc<0) return rc;
    out->tv_sec=ts.tv_sec;
    out->tv_usec=ts.tv_nsec/1000LL;
    return 0;
}
