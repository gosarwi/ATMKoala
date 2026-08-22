#ifndef ATM_RTC_H
#define ATM_RTC_H

#include <stdint.h>

typedef struct {
    int year, month, day;
    int hour, minute, second;
} rtc_datetime_t;

/* Reads a coherent CMOS RTC snapshot. Returns 0 on a validated date, -1 when
 * firmware has no usable clock or returns an invalid calendar value. */
int rtc_read_datetime(rtc_datetime_t *out);

/* Explicit hardware-clock writeback. Callers must perform user confirmation;
 * this API validates a civil UTC value, preserves firmware data mode and
 * verifies a coherent readback. */
int rtc_write_datetime(const rtc_datetime_t *in);
int rtc_write_selftest(void);

#endif
