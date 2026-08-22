#ifndef ATM_LIBC_SIGNAL_H
#define ATM_LIBC_SIGNAL_H

/* ATMKoala currently supports `kill` only for a direct child and only for
 * SIGTERM/SIGKILL. `kill(pid, 0)` is a non-delivering self/direct-child
 * existence probe. There is no signal handler, mask, pending-set, stop or
 * continue API yet. */
#define SIGKILL 9
#define SIGTERM 15

#endif
