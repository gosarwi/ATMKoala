/* ATMKoala freestanding fcntl wrapper — bounded descriptor/status flags. */
#include <fcntl.h>
#include <errno.h>
#include "atm_native_abi.h"
#include "internal.h"

int fcntl(int fd,int cmd,...){
    uint32_t arg=0;
    if(cmd==F_SETFL || cmd==F_SETFD || cmd==F_DUPFD){
        __builtin_va_list ap;
        __builtin_va_start(ap,cmd);
        arg=(uint32_t)__builtin_va_arg(ap,int);
        __builtin_va_end(ap);
    } else if(cmd!=F_GETFL && cmd!=F_GETFD){
        errno=EINVAL;
        return -1;
    }
    return (int)__atm_sysret(atm_fcntl(fd,cmd,arg));
}
