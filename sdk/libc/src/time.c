/* ATMKoala native libc time wrappers — MIT licensed project code. */
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "atm_native_abi.h"
#include "internal.h"

int clock_gettime(clockid_t clock_id,struct timespec *tp){
    if(!tp) return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_clock_gettime(clock_id,(atm_clock_timespec_t *)tp));
}

int clock_getres(clockid_t clock_id,struct timespec *res){
    if(!res) return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_clock_getres(clock_id,(atm_clock_timespec_t *)res));
}

int nanosleep(const struct timespec *req,struct timespec *rem){
    if(!req) return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_nanosleep((const atm_clock_timespec_t *)req,(atm_clock_timespec_t *)rem));
}

int gettimeofday(struct timeval *tv,void *tz){
    (void)tz;
    if(!tv) return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_gettimeofday((atm_timeval_t *)tv));
}

time_t time(time_t *out){
    struct timeval tv;
    if(gettimeofday(&tv,0)<0) return (time_t)-1;
    if(out) *out=tv.tv_sec;
    return tv.tv_sec;
}
