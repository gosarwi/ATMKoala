#ifndef ATM_SYSCALL_H
#define ATM_SYSCALL_H

/*
 * ATMKoala syscall ABI v11.
 *
 * Register contract: RAX=number, RDI/RSI/RDX/R10/R8/R9=arguments,
 * RAX=result or a negative errno-style value. Native static CPL3 tasks enter
 * through int $0x80 with checked user-copy; this remains a bounded ATMKoala
 * ABI rather than the Linux syscall or binary ABI.
 */
#include <stdint.h>
#include "idt.h"

#define ATM_SYSCALL_ABI_V1 1u
#define ATM_SYSCALL_ABI_V2 2u
#define ATM_SYSCALL_ABI_V3 3u
#define ATM_SYSCALL_ABI_V4 4u
#define ATM_SYSCALL_ABI_V5 5u
#define ATM_SYSCALL_ABI_V6 6u
#define ATM_SYSCALL_ABI_V7 7u
#define ATM_SYSCALL_ABI_V8 8u
#define ATM_SYSCALL_ABI_V9 9u
#define ATM_SYSCALL_ABI_V10 10u
#define ATM_SYSCALL_ABI_V11 11u

#define ATM_SYS_READ       0u
#define ATM_SYS_WRITE      1u
#define ATM_SYS_OPEN       2u
#define ATM_SYS_CLOSE      3u
#define ATM_SYS_STAT       4u
#define ATM_SYS_FSTAT      5u
#define ATM_SYS_LSTAT      6u
#define ATM_SYS_LSEEK      8u
#define ATM_SYS_PREAD64    17u
#define ATM_SYS_PWRITE64   18u
#define ATM_SYS_READV      19u
#define ATM_SYS_WRITEV     20u
#define ATM_SYS_ACCESS     21u
#define ATM_SYS_DUP        32u
#define ATM_SYS_DUP2       33u
#define ATM_SYS_FSYNC      74u
#define ATM_SYS_FDATASYNC  75u
#define ATM_SYS_TRUNCATE   76u
#define ATM_SYS_FTRUNCATE  77u
#define ATM_SYS_GETCWD     79u
#define ATM_SYS_CHDIR      80u
#define ATM_SYS_RENAME     82u
#define ATM_SYS_MKDIR      83u
#define ATM_SYS_RMDIR      84u
#define ATM_SYS_LINK       86u
#define ATM_SYS_UNLINK     87u
#define ATM_SYS_SYMLINK    88u
#define ATM_SYS_READLINK   89u
#define ATM_SYS_CHMOD      90u
#define ATM_SYS_UMASK      95u
#define ATM_SYS_BRK        12u
#define ATM_SYS_EXECVE     59u
#define ATM_SYS_EXIT       60u
#define ATM_SYS_WAITPID    61u
#define ATM_SYS_KILL       62u
#define ATM_SYS_GETPID     39u
#define ATM_SYS_GETPPID    110u
#define ATM_SYS_GETUID     102u
#define ATM_SYS_GETGID     104u
#define ATM_SYS_GETTID     186u
#define ATM_SYS_ABI_INFO   0xA700u
#define ATM_SYS_ISATTY     0xA701u
#define ATM_SYS_PIPE       0xA702u
#define ATM_SYS_OPENDIR    0xA703u
#define ATM_SYS_READDIR    0xA704u
#define ATM_SYS_CLOSEDIR   0xA705u
#define ATM_SYS_FCNTL      0xA706u
#define ATM_SYS_POLL       0xA707u
#define ATM_SYS_CLOCK_GETTIME 0xA708u
#define ATM_SYS_GETTIMEOFDAY  0xA709u
#define ATM_SYS_SELECT        0xA70Au
#define ATM_SYS_CLOCK_GETRES  0xA70Bu
#define ATM_SYS_UNAME         0xA70Cu
#define ATM_SYS_NANOSLEEP     0xA70Du
#define ATM_SYS_SOCKET     0xA710u
#define ATM_SYS_CONNECT    0xA711u
#define ATM_SYS_BIND       0xA712u
#define ATM_SYS_LISTEN     0xA713u
#define ATM_SYS_ACCEPT     0xA714u
#define ATM_SYS_OPENAT     0xA715u
#define ATM_SYS_FSTATAT    0xA716u
#define ATM_SYS_SEND       0xA717u
#define ATM_SYS_RECV       0xA718u

#define ATM_EFAULT  14
#define ATM_ENOMEM  12
#define ATM_ENOSYS  38
#define ATM_EINVAL  22
#define ATM_EPIPE   32
#define ATM_ECHILD  10
#define ATM_EAGAIN  11

uint64_t atm_syscall_dispatch(registers_t *r);
uint32_t atm_syscall_abi_version(void);
int      atm_syscall_selftest(void);

#endif
