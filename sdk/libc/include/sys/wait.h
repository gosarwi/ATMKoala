#ifndef ATM_LIBC_SYS_WAIT_H
#define ATM_LIBC_SYS_WAIT_H

#include <unistd.h>

/* ATMKoala v0.9 supports a validated blocking waitpid path. WNOHANG returns
 * 0 while a matching child remains live; unsupported wait options fail. */
#define WNOHANG 1

#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0)
#define WTERMSIG(status) ((status) & 0x7f)

pid_t waitpid(pid_t pid,int *status,int options);

#endif
