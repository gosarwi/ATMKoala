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

/* ATMKoala accepts up to 16 task-owned descriptors and finite timeouts up to
 * 600000 ms. Pipes provide bounded data/space/HUP readiness. TCP sockets expose
 * only local state: established POLLOUT, buffered out-of-order POLLIN, terminal
 * POLLHUP/POLLERR. It does not poll the wire or make listeners accept-ready. */
int poll(struct pollfd *fds,nfds_t nfds,int timeout);

#endif
