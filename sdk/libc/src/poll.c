#include <poll.h>
#include <errno.h>
#include "atm_native_abi.h"
#include "internal.h"

int poll(struct pollfd *fds,nfds_t nfds,int timeout){
    /* Finite pipe-only waits are PIT-quantized by the kernel. Infinite waits
     * remain unsupported until signal interruption semantics exist. */
    if(timeout<0 || timeout>600000 || nfds>16 || (nfds && !fds)){
        errno=EINVAL;
        return -1;
    }
    return (int)__atm_sysret(atm_poll((atm_pollfd_t *)fds,nfds,(uint64_t)timeout));
}
