#include "rtc.h"
#include "util.h"
#include <stdint.h>

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71
#define RTC_REG_A  0x0A
#define RTC_REG_B  0x0B
#define RTC_SEC    0x00
#define RTC_MIN    0x02
#define RTC_HOUR   0x04
#define RTC_DAY    0x07
#define RTC_MONTH  0x08
#define RTC_YEAR   0x09
#define RTC_CENTURY 0x32

static uint8_t cmos_read(uint8_t reg){ outb(CMOS_INDEX,reg); return inb(CMOS_DATA); }
static void cmos_write(uint8_t reg,uint8_t value){outb(CMOS_INDEX,reg);outb(CMOS_DATA,value);}
static int rtc_not_updating(void){
    for(int i=0;i<100000;i++) if(!(cmos_read(RTC_REG_A)&0x80)) return 1;
    return 0;
}
static int bcd_to_bin(uint8_t v){return (v&0x0F)+((v>>4)*10);}
static uint8_t bin_to_bcd(uint8_t v){return (uint8_t)(((v/10u)<<4)|(v%10u));}
static int leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
static int days_in_month(int y,int m){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return m==2?d[1]+leap(y):d[m-1];}
static int valid(const rtc_datetime_t *t){return t&&t->year>=2000&&t->year<=2099&&t->month>=1&&t->month<=12&&t->day>=1&&t->day<=days_in_month(t->year,t->month)&&t->hour>=0&&t->hour<24&&t->minute>=0&&t->minute<60&&t->second>=0&&t->second<60;}

static int rtc_encode(const rtc_datetime_t *in,uint8_t reg_b,uint8_t raw[7]){
    if(!valid(in)||!raw)return -1;int hour=in->hour,pm=0;
    if(!(reg_b&0x02)){pm=hour>=12;hour%=12;if(hour==0)hour=12;}
    raw[0]=(uint8_t)in->second;raw[1]=(uint8_t)in->minute;raw[2]=(uint8_t)hour;raw[3]=(uint8_t)in->day;raw[4]=(uint8_t)in->month;raw[5]=(uint8_t)(in->year%100);raw[6]=(uint8_t)(in->year/100);
    if(!(reg_b&0x04))for(int i=0;i<7;i++)raw[i]=bin_to_bcd(raw[i]);
    if(!(reg_b&0x02)&&pm)raw[2]|=0x80;return 0;
}

int rtc_read_datetime(rtc_datetime_t *out){
    if(!out)return -1;
    for(int attempt=0;attempt<4;attempt++){
        if(!rtc_not_updating())return -1;
        uint8_t b=cmos_read(RTC_REG_B);
        uint8_t sec=cmos_read(RTC_SEC),min=cmos_read(RTC_MIN),hour=cmos_read(RTC_HOUR);
        uint8_t day=cmos_read(RTC_DAY),mon=cmos_read(RTC_MONTH),year=cmos_read(RTC_YEAR),cent=cmos_read(RTC_CENTURY);
        if(!rtc_not_updating())continue;
        /* Re-read a complete snapshot rather than accept a second-boundary mix. */
        uint8_t sec2=cmos_read(RTC_SEC),min2=cmos_read(RTC_MIN),hour2=cmos_read(RTC_HOUR);
        uint8_t day2=cmos_read(RTC_DAY),mon2=cmos_read(RTC_MONTH),year2=cmos_read(RTC_YEAR),cent2=cmos_read(RTC_CENTURY);
        if(sec!=sec2||min!=min2||hour!=hour2||day!=day2||mon!=mon2||year!=year2||cent!=cent2)continue;
        if(!(b&0x04)){sec=(uint8_t)bcd_to_bin(sec);min=(uint8_t)bcd_to_bin(min);hour=(uint8_t)bcd_to_bin(hour&0x7F);day=(uint8_t)bcd_to_bin(day);mon=(uint8_t)bcd_to_bin(mon);year=(uint8_t)bcd_to_bin(year);cent=(uint8_t)bcd_to_bin(cent);}
        if(!(b&0x02)){int pm=hour&0x80;hour&=0x7F;if(pm){if(hour!=12)hour=(uint8_t)(hour+12);}else if(hour==12)hour=0;}
        rtc_datetime_t t;t.year=(cent>=20&&cent<=21)?cent*100+year:2000+year;t.month=mon;t.day=day;t.hour=hour;t.minute=min;t.second=sec;
        if(!valid(&t))return -1;*out=t;return 0;
    }
    return -1;
}

int rtc_write_datetime(const rtc_datetime_t *in){
    uint8_t raw[7];if(!valid(in)||!rtc_not_updating())return -1;uint8_t b=cmos_read(RTC_REG_B);if(rtc_encode(in,b,raw)<0)return -1;
    cmos_write(RTC_REG_B,(uint8_t)(b|0x80));
    cmos_write(RTC_SEC,raw[0]);cmos_write(RTC_MIN,raw[1]);cmos_write(RTC_HOUR,raw[2]);cmos_write(RTC_DAY,raw[3]);cmos_write(RTC_MONTH,raw[4]);cmos_write(RTC_YEAR,raw[5]);cmos_write(RTC_CENTURY,raw[6]);
    cmos_write(RTC_REG_B,b);rtc_datetime_t check;if(rtc_read_datetime(&check)<0)return -1;
    return check.year==in->year&&check.month==in->month&&check.day==in->day&&check.hour==in->hour&&check.minute==in->minute?0:-1;
}
int rtc_write_selftest(void){
    rtc_datetime_t t={2026,2,3,13,4,5};uint8_t raw[7];if(rtc_encode(&t,0x06,raw)<0||raw[0]!=5||raw[1]!=4||raw[2]!=13||raw[3]!=3||raw[4]!=2||raw[5]!=26||raw[6]!=20)return -1;
    if(rtc_encode(&t,0x00,raw)<0||raw[0]!=0x05||raw[1]!=0x04||raw[2]!=0x81||raw[3]!=0x03||raw[4]!=0x02||raw[5]!=0x26||raw[6]!=0x20)return -1;
    t.month=13;return rtc_encode(&t,0x06,raw)<0?0:-1;
}
