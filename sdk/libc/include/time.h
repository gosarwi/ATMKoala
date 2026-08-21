#ifndef ATM_LIBC_TIME_H
#define ATM_LIBC_TIME_H

#include <stdint.h>

typedef int64_t time_t;
typedef int clockid_t;

struct timespec {
    time_t tv_sec;
    int64_t tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int clock_gettime(clockid_t clock_id,struct timespec *tp);
int clock_getres(clockid_t clock_id,struct timespec *res);
/* PIT-quantized task-aware sleep; no signal interruption semantics yet. */
int nanosleep(const struct timespec *req,struct timespec *rem);
time_t time(time_t *out);

#endif
