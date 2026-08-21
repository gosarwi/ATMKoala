#ifndef ATM_LIBC_SYS_TIME_H
#define ATM_LIBC_SYS_TIME_H

#include <stdint.h>
#include <time.h>

struct timeval {
    time_t tv_sec;
    int64_t tv_usec;
};

int gettimeofday(struct timeval *tv,void *tz);

#endif
