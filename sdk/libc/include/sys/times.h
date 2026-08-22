#ifndef ATM_LIBC_SYS_TIMES_H
#define ATM_LIBC_SYS_TIMES_H

#include <stdint.h>

typedef int64_t clock_t;

/* ATMKoala currently accounts only the calling task's scheduler running ticks.
 * System and reaped-child fields are defined for source compatibility and are
 * reported as zero until kernel accounting grows those distinct domains. */
struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buf);

#endif
