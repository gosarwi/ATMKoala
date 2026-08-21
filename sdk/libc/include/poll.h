#ifndef ATM_LIBC_POLL_H
#define ATM_LIBC_POLL_H

#include <stdint.h>

typedef uint64_t nfds_t;

struct pollfd {
    int fd;
    int16_t events;
    int16_t revents;
};

#define POLLIN   0x0001
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

/* ATMKoala v0.9 currently supports timeout == 0 and task-owned pipe FDs.
 * Other descriptor classes return POLLNVAL; blocking timeouts are rejected. */
int poll(struct pollfd *fds,nfds_t nfds,int timeout);

#endif
