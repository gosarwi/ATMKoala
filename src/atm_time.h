#ifndef ATM_TIME_H
#define ATM_TIME_H

#include <stdint.h>
#include "rtc.h"

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} atm_timespec_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} atm_timeval_t;

#define ATM_CLOCK_REALTIME  0
#define ATM_CLOCK_MONOTONIC 1

/* Returns 0 on success or a negative ATM errno-style result. Realtime derives
 * from the firmware RTC and deliberately has no timezone/NTP adjustment. */
int atm_clock_gettime(int clock_id, atm_timespec_t *out);
/* The tick-backed source resolution is 10 ms for the supported realtime and
 * monotonic clocks; unsupported IDs return -ATM_EINVAL. */
int atm_clock_getres(int clock_id, atm_timespec_t *out);
int atm_gettimeofday(atm_timeval_t *out);

/* Convert a validated UTC calendar value into a local civil time for one of
 * ATMKoala's embedded IANA-style zone presets. `offset_minutes` is the
 * applied UTC offset and `dst_active` is zero for fixed-offset zones. */
int atm_timezone_convert(const char *zone,const rtc_datetime_t *utc,
                         rtc_datetime_t *local,int *offset_minutes,int *dst_active);
int atm_timezone_supported(const char *zone);
/* Read the firmware RTC as UTC, then convert it through the selected zone. */
int atm_local_datetime(const char *zone,rtc_datetime_t *local,
                       int *offset_minutes,int *dst_active);
/* Pure date/rule checks; no CMOS, PIT, framebuffer or persistent-state I/O. */
int atm_timezone_selftest(void);

#endif
