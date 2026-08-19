/* ATMKoala native libc errno — MIT licensed project code. */
#include <errno.h>
#include <stdint.h>

static int atm_errno_value=0;

int *__errno_location(void){ return &atm_errno_value; }

int64_t __atm_sysret(int64_t value){
    if(value<0){
        int code=(int)-value;
        errno=(code>0 && code<4096)?code:EINVAL;
        return -1;
    }
    return value;
}
