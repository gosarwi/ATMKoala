#ifndef ATM_LINUX_ABI_H
#define ATM_LINUX_ABI_H

#include <stdint.h>
#include "idt.h"

/* Linux x86-64 compatibility L3: L0 process I/O, L1 bounded virtual-memory
 * calls, L2 TLS/identity calls, plus L3 AT_FDCWD-only openat/newfstatat and
 * fd-backed getdents64. This is not a claim of a full Linux ABI or musl runtime. */
#define ATM_LINUX_ABI_LEVEL 3u

void linux_abi_init(void);
void linux_abi_set_kernel_stack(uint64_t stack_top);
/* Restore the active task’s bounded user TLS base before returning to CPL3. */
void linux_abi_set_fs_base(uint64_t fs_base);
int  linux_abi_ready(void);
uint64_t linux_syscall_dispatch(registers_t *r);

/* Consumed by the assembly syscall entry while interrupts are masked. */
extern uint64_t linux_kernel_stack_top;
extern uint64_t linux_saved_user_rsp;

#endif
