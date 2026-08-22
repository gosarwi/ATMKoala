#ifndef ATM_SYSCALL_H
#define ATM_SYSCALL_H

/*
 * ATMKoala syscall ABI v22.
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
#define ATM_SYSCALL_ABI_V12 12u
#define ATM_SYSCALL_ABI_V13 13u
#define ATM_SYSCALL_ABI_V14 14u
#define ATM_SYSCALL_ABI_V15 15u
#define ATM_SYSCALL_ABI_V16 16u
#define ATM_SYSCALL_ABI_V17 17u
#define ATM_SYSCALL_ABI_V18 18u
#define ATM_SYSCALL_ABI_V19 19u
#define ATM_SYSCALL_ABI_V20 20u
#define ATM_SYSCALL_ABI_V21 21u
#define ATM_SYSCALL_ABI_V22 22u

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
#define ATM_SYS_CHOWN      92u
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
#define ATM_SYS_GETEUID    107u
#define ATM_SYS_GETEGID    108u
#define ATM_SYS_GETRESUID  118u
#define ATM_SYS_GETRESGID  120u
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
#define ATM_SYS_DUP3       0xA719u
#define ATM_SYS_PIPE2      0xA71Au
#define ATM_SYS_GETPGID    0xA71Bu
#define ATM_SYS_SETPGID    0xA71Cu
#define ATM_SYS_GETSID     0xA71Du
#define ATM_SYS_SETSID     0xA71Eu
#define ATM_SYS_PREADV     0xA71Fu
#define ATM_SYS_PWRITEV    0xA720u
#define ATM_SYS_FCHDIR     0xA721u
#define ATM_SYS_GETRUSAGE   0xA722u
#define ATM_SYS_MKDIRAT     0xA723u
#define ATM_SYS_UNLINKAT    0xA724u
#define ATM_SYS_RENAMEAT    0xA725u
#define ATM_SYS_LINKAT      0xA726u
#define ATM_SYS_SYMLINKAT   0xA727u
#define ATM_SYS_READLINKAT  0xA728u
#define ATM_SYS_TIMES       0xA729u
#define ATM_SYS_FCHMOD      0xA72Au
#define ATM_SYS_FCHOWN      0xA72Bu
#define ATM_SYS_GETRLIMIT    0xA72Cu
#define ATM_SYS_FACCESSAT    0xA72Du

#define ATM_EBADF   9
#define ATM_EFAULT  14
#define ATM_ENOTDIR 20
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
