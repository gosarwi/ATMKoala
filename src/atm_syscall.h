#ifndef ATM_SYSCALL_H
#define ATM_SYSCALL_H

/*
 * ATMKoala syscall ABI v1.
 *
 * Register contract: RAX=number, RDI/RSI/RDX/R10/R8/R9=arguments,
 * RAX=result or a negative errno-style value.  The current IDT gate is
 * ring-0 only: this ABI is a tested foundation, not yet a user-mode ABI.
 */
#include <stdint.h>
#include "idt.h"

#define ATM_SYSCALL_ABI_V1 1u
#define ATM_SYSCALL_ABI_V2 2u

#define ATM_SYS_READ       0u
#define ATM_SYS_WRITE      1u
#define ATM_SYS_OPEN       2u
#define ATM_SYS_CLOSE      3u
#define ATM_SYS_FSTAT      5u
#define ATM_SYS_LSEEK      8u
#define ATM_SYS_BRK        12u
#define ATM_SYS_EXIT       60u
#define ATM_SYS_WAITPID    61u
#define ATM_SYS_KILL       62u
#define ATM_SYS_GETPID     39u
#define ATM_SYS_GETPPID    110u
#define ATM_SYS_GETUID     102u
#define ATM_SYS_GETGID     104u
#define ATM_SYS_GETTID     186u
#define ATM_SYS_ABI_INFO   0xA700u
#define ATM_SYS_SOCKET     0xA710u
#define ATM_SYS_CONNECT    0xA711u
#define ATM_SYS_BIND       0xA712u
#define ATM_SYS_LISTEN     0xA713u
#define ATM_SYS_ACCEPT     0xA714u

#define ATM_EFAULT  14
#define ATM_ENOMEM  12
#define ATM_ENOSYS  38
#define ATM_EINVAL  22

uint64_t atm_syscall_dispatch(registers_t *r);
uint32_t atm_syscall_abi_version(void);
int      atm_syscall_selftest(void);

#endif
