/* ATMKoala native libc select wrapper — bounded zero-timeout pipe readiness. */
#include <sys/select.h>
#include "atm_native_abi.h"
#include "internal.h"

int select(int nfds,fd_set *readfds,fd_set *writefds,fd_set *exceptfds,struct timeval *timeout){
    return (int)__atm_sysret(atm_select(nfds,(atm_fdset_t *)readfds,(atm_fdset_t *)writefds,(atm_fdset_t *)exceptfds,(const atm_timeval_t *)timeout));
}
