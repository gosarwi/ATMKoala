#ifndef ATM_NATIVE_ABI_H
#define ATM_NATIVE_ABI_H

/* ATM Native App ABI v1 public header.
 * Include this header from a freestanding static x86-64 application linked for
 * ATMKoala. It is intentionally not a Linux libc replacement. Calls return a
 * non-negative result or a negative ATM errno-style value; applications may
 * translate errors into their own errno storage if desired. */

#include <stdint.h>

#define ATM_NATIVE_ABI_MAJOR 1u
#define ATM_NATIVE_ABI_MINOR 12u

#define ATM_SYS_READ       0u
#define ATM_SYS_WRITE      1u
#define ATM_SYS_OPEN       2u
#define ATM_SYS_CLOSE      3u
#define ATM_SYS_STAT       4u
#define ATM_SYS_FSTAT      5u
#define ATM_SYS_LSTAT      6u
#define ATM_SYS_LSEEK      8u
#define ATM_SYS_PREAD64   17u
#define ATM_SYS_PWRITE64  18u
#define ATM_SYS_READV     19u
#define ATM_SYS_WRITEV    20u
#define ATM_SYS_ACCESS    21u
#define ATM_SYS_DUP       32u
#define ATM_SYS_DUP2      33u
#define ATM_SYS_FSYNC     74u
#define ATM_SYS_FDATASYNC 75u
#define ATM_SYS_TRUNCATE  76u
#define ATM_SYS_FTRUNCATE 77u
#define ATM_SYS_GETCWD    79u
#define ATM_SYS_CHDIR     80u
#define ATM_SYS_RENAME    82u
#define ATM_SYS_MKDIR     83u
#define ATM_SYS_RMDIR     84u
#define ATM_SYS_LINK      86u
#define ATM_SYS_UNLINK    87u
#define ATM_SYS_SYMLINK   88u
#define ATM_SYS_READLINK  89u
#define ATM_SYS_CHMOD     90u
#define ATM_SYS_UMASK     95u
#define ATM_SYS_BRK       12u
#define ATM_SYS_EXECVE    59u
#define ATM_SYS_EXIT      60u
#define ATM_SYS_WAITPID   61u
#define ATM_SYS_KILL      62u
#define ATM_SYS_GETPID    39u
#define ATM_SYS_GETPPID  110u
#define ATM_SYS_GETUID   102u
#define ATM_SYS_GETGID   104u
#define ATM_SYS_GETTID   186u
#define ATM_SYS_ABI_INFO 0xA700u
#define ATM_SYS_ISATTY   0xA701u
#define ATM_SYS_PIPE     0xA702u
#define ATM_SYS_OPENDIR  0xA703u
#define ATM_SYS_READDIR  0xA704u
#define ATM_SYS_CLOSEDIR 0xA705u
#define ATM_SYS_FCNTL    0xA706u
#define ATM_SYS_POLL       0xA707u
#define ATM_SYS_CLOCK_GETTIME 0xA708u
#define ATM_SYS_GETTIMEOFDAY  0xA709u
#define ATM_SYS_SELECT        0xA70Au
#define ATM_SYS_CLOCK_GETRES  0xA70Bu
#define ATM_SYS_UNAME         0xA70Cu
#define ATM_SYS_NANOSLEEP     0xA70Du

#define ATM_SYS_SOCKET   0xA710u
#define ATM_SYS_CONNECT  0xA711u
#define ATM_SYS_BIND     0xA712u
#define ATM_SYS_LISTEN   0xA713u
#define ATM_SYS_ACCEPT   0xA714u
#define ATM_SYS_OPENAT   0xA715u
#define ATM_SYS_FSTATAT  0xA716u
#define ATM_SYS_SEND     0xA717u
#define ATM_SYS_RECV     0xA718u

typedef struct { void *iov_base; uint64_t iov_len; } atm_iovec_t;
#define ATM_NAME_MAX 255
#define ATM_O_ACCMODE 0x0003u
#define ATM_AT_FDCWD (-100)
#define ATM_AT_SYMLINK_NOFOLLOW 0x100u
#define ATM_O_NONBLOCK 0x4000u
#define ATM_F_DUPFD 0
#define ATM_F_GETFL 3
#define ATM_F_SETFL 4
/* Fixed packed wire record copied from kernel VFS to user memory. */
typedef struct __attribute__((packed)) { uint64_t d_ino; uint8_t d_type; char d_name[ATM_NAME_MAX+1]; } atm_dirent_t;
typedef struct { int fd; uint16_t events; uint16_t revents; } atm_pollfd_t;
typedef struct { int64_t tv_sec; int64_t tv_nsec; } atm_clock_timespec_t;
typedef struct { int64_t tv_sec; int64_t tv_usec; } atm_timeval_t;
#define ATM_UTS_FIELD_LEN 65u
typedef struct {
    char sysname[ATM_UTS_FIELD_LEN];
    char nodename[ATM_UTS_FIELD_LEN];
    char release[ATM_UTS_FIELD_LEN];
    char version[ATM_UTS_FIELD_LEN];
    char machine[ATM_UTS_FIELD_LEN];
    char domainname[ATM_UTS_FIELD_LEN];
} atm_utsname_t;
typedef struct { uint64_t bits; } atm_fdset_t;
#define ATM_FD_SETSIZE 64
#define ATM_CLOCK_REALTIME 0
#define ATM_CLOCK_MONOTONIC 1
#define ATM_POLLIN   0x0001u
#define ATM_POLLOUT  0x0004u
#define ATM_POLLERR  0x0008u
#define ATM_POLLHUP  0x0010u
#define ATM_POLLNVAL 0x0020u
#define ATM_MSG_DONTWAIT 0x40u

#define ATM_EFAULT 14
#define ATM_ENOMEM 12
#define ATM_EINVAL 22
#define ATM_ENOSYS 38
#define ATM_ECHILD 10
#define ATM_EPIPE 32

#define ATM_O_RDONLY 0x0000u
#define ATM_O_WRONLY 0x0001u
#define ATM_O_RDWR   0x0002u
#define ATM_O_CREAT  0x0040u
#define ATM_O_TRUNC  0x0200u
#define ATM_SEEK_SET 0
#define ATM_SEEK_CUR 1
#define ATM_SEEK_END 2
#define ATM_WNOHANG 1

static inline int64_t atm_syscall0(uint64_t number){
    register uint64_t rax __asm__("rax")=number;
    __asm__ volatile("int $0x80" : "+a"(rax) :: "memory","rcx","r11");
    return (int64_t)rax;
}

static inline int64_t atm_syscall1(uint64_t number,uint64_t a1){
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a1;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi) : "memory","rcx","r11");
    return (int64_t)rax;
}

static inline int64_t atm_syscall2(uint64_t number,uint64_t a1,uint64_t a2){
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a1;
    register uint64_t rsi __asm__("rsi")=a2;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi),"S"(rsi) : "memory","rcx","r11");
    return (int64_t)rax;
}

static inline int64_t atm_syscall3(uint64_t number,uint64_t a1,uint64_t a2,uint64_t a3){
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a1;
    register uint64_t rsi __asm__("rsi")=a2;
    register uint64_t rdx __asm__("rdx")=a3;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi),"S"(rsi),"d"(rdx) : "memory","rcx","r11");
    return (int64_t)rax;
}
static inline int64_t atm_syscall4(uint64_t number,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4){
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a1;
    register uint64_t rsi __asm__("rsi")=a2;
    register uint64_t rdx __asm__("rdx")=a3;
    register uint64_t r10 __asm__("r10")=a4;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi),"S"(rsi),"d"(rdx),"r"(r10) : "memory","rcx","r11");
    return (int64_t)rax;
}
static inline int64_t atm_syscall5(uint64_t number,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a1;
    register uint64_t rsi __asm__("rsi")=a2;
    register uint64_t rdx __asm__("rdx")=a3;
    register uint64_t r10 __asm__("r10")=a4;
    register uint64_t r8 __asm__("r8")=a5;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi),"S"(rsi),"d"(rdx),"r"(r10),"r"(r8) : "memory","rcx","r11");
    return (int64_t)rax;
}

static inline int64_t atm_open(const char *path,uint32_t flags,uint32_t mode){
    return atm_syscall3(ATM_SYS_OPEN,(uint64_t)(uintptr_t)path,flags,mode);
}
static inline int64_t atm_openat(int dirfd,const char *path,uint32_t flags,uint32_t mode){
    return atm_syscall4(ATM_SYS_OPENAT,(uint64_t)(int64_t)dirfd,(uint64_t)(uintptr_t)path,flags,mode);
}
static inline int64_t atm_close(int fd){ return atm_syscall1(ATM_SYS_CLOSE,(uint64_t)fd); }
static inline int64_t atm_read(int fd,void *buf,uint64_t count){
    return atm_syscall3(ATM_SYS_READ,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count);
}
static inline int64_t atm_write(int fd,const void *buf,uint64_t count){
    return atm_syscall3(ATM_SYS_WRITE,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count);
}
static inline int64_t atm_lseek(int fd,int64_t offset,int whence){
    return atm_syscall3(ATM_SYS_LSEEK,(uint64_t)fd,(uint64_t)offset,(uint64_t)whence);
}
static inline int64_t atm_fstat(int fd,void *st){
    return atm_syscall2(ATM_SYS_FSTAT,(uint64_t)fd,(uint64_t)(uintptr_t)st);
}
static inline int64_t atm_stat(const char *path,void *st){return atm_syscall2(ATM_SYS_STAT,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)st);}
static inline int64_t atm_lstat(const char *path,void *st){return atm_syscall2(ATM_SYS_LSTAT,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)st);}
static inline int64_t atm_fstatat(int dirfd,const char *path,void *st,uint32_t flags){return atm_syscall4(ATM_SYS_FSTATAT,(uint64_t)(int64_t)dirfd,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)st,flags);}
static inline int64_t atm_pread(int fd,void *buf,uint64_t count,uint64_t offset){return atm_syscall4(ATM_SYS_PREAD64,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count,offset);}
static inline int64_t atm_pwrite(int fd,const void *buf,uint64_t count,uint64_t offset){return atm_syscall4(ATM_SYS_PWRITE64,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count,offset);}
static inline int64_t atm_readv(int fd,const atm_iovec_t *iov,int iovcnt){return atm_syscall3(ATM_SYS_READV,(uint64_t)fd,(uint64_t)(uintptr_t)iov,(uint64_t)iovcnt);}
static inline int64_t atm_writev(int fd,const atm_iovec_t *iov,int iovcnt){return atm_syscall3(ATM_SYS_WRITEV,(uint64_t)fd,(uint64_t)(uintptr_t)iov,(uint64_t)iovcnt);}
static inline int64_t atm_dup(int fd){return atm_syscall1(ATM_SYS_DUP,(uint64_t)fd);}
static inline int64_t atm_dup2(int fd,int newfd){return atm_syscall2(ATM_SYS_DUP2,(uint64_t)fd,(uint64_t)newfd);}
static inline int64_t atm_fsync(int fd){return atm_syscall1(ATM_SYS_FSYNC,(uint64_t)fd);}
static inline int64_t atm_fdatasync(int fd){return atm_syscall1(ATM_SYS_FDATASYNC,(uint64_t)fd);}
static inline int64_t atm_truncate(const char *path,uint64_t size){return atm_syscall2(ATM_SYS_TRUNCATE,(uint64_t)(uintptr_t)path,size);}
static inline int64_t atm_ftruncate(int fd,uint64_t size){return atm_syscall2(ATM_SYS_FTRUNCATE,(uint64_t)fd,size);}
static inline int64_t atm_getcwd(char *buf,uint64_t size){return atm_syscall2(ATM_SYS_GETCWD,(uint64_t)(uintptr_t)buf,size);}
static inline int64_t atm_chdir(const char *path){return atm_syscall1(ATM_SYS_CHDIR,(uint64_t)(uintptr_t)path);}
static inline int64_t atm_rename(const char *oldpath,const char *newpath){return atm_syscall2(ATM_SYS_RENAME,(uint64_t)(uintptr_t)oldpath,(uint64_t)(uintptr_t)newpath);}
static inline int64_t atm_access(const char *path,int mode){return atm_syscall2(ATM_SYS_ACCESS,(uint64_t)(uintptr_t)path,(uint64_t)mode);}
static inline int64_t atm_mkdir(const char *path,uint32_t mode){return atm_syscall2(ATM_SYS_MKDIR,(uint64_t)(uintptr_t)path,mode);}
static inline int64_t atm_rmdir(const char *path){return atm_syscall1(ATM_SYS_RMDIR,(uint64_t)(uintptr_t)path);}
static inline int64_t atm_link(const char *oldpath,const char *newpath){return atm_syscall2(ATM_SYS_LINK,(uint64_t)(uintptr_t)oldpath,(uint64_t)(uintptr_t)newpath);}
static inline int64_t atm_unlink(const char *path){return atm_syscall1(ATM_SYS_UNLINK,(uint64_t)(uintptr_t)path);}
static inline int64_t atm_symlink(const char *target,const char *linkpath){return atm_syscall2(ATM_SYS_SYMLINK,(uint64_t)(uintptr_t)target,(uint64_t)(uintptr_t)linkpath);}
static inline int64_t atm_readlink(const char *path,char *buf,uint64_t size){return atm_syscall3(ATM_SYS_READLINK,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)buf,size);}
static inline int64_t atm_chmod(const char *path,uint32_t mode){return atm_syscall2(ATM_SYS_CHMOD,(uint64_t)(uintptr_t)path,mode);}
static inline uint64_t atm_umask(uint32_t mask){return (uint64_t)atm_syscall1(ATM_SYS_UMASK,mask);}
static inline int64_t atm_isatty(int fd){return atm_syscall1(ATM_SYS_ISATTY,(uint64_t)fd);}
static inline int64_t atm_pipe(int fds[2]){return atm_syscall1(ATM_SYS_PIPE,(uint64_t)(uintptr_t)fds);}
static inline int64_t atm_opendir(const char *path){return atm_syscall1(ATM_SYS_OPENDIR,(uint64_t)(uintptr_t)path);}
static inline int64_t atm_readdir(int handle,atm_dirent_t *entry){return atm_syscall2(ATM_SYS_READDIR,(uint64_t)handle,(uint64_t)(uintptr_t)entry);}
static inline int64_t atm_closedir(int handle){return atm_syscall1(ATM_SYS_CLOSEDIR,(uint64_t)handle);}
static inline int64_t atm_fcntl(int fd,int cmd,uint32_t arg){return atm_syscall3(ATM_SYS_FCNTL,(uint64_t)fd,(uint64_t)cmd,arg);}
static inline int64_t atm_poll(atm_pollfd_t *fds,uint64_t nfds,uint64_t timeout){return atm_syscall3(ATM_SYS_POLL,(uint64_t)(uintptr_t)fds,nfds,timeout);}
static inline int64_t atm_clock_gettime(int clock_id,atm_clock_timespec_t *out){return atm_syscall2(ATM_SYS_CLOCK_GETTIME,(uint64_t)clock_id,(uint64_t)(uintptr_t)out);}
static inline int64_t atm_clock_getres(int clock_id,atm_clock_timespec_t *out){return atm_syscall2(ATM_SYS_CLOCK_GETRES,(uint64_t)clock_id,(uint64_t)(uintptr_t)out);}
static inline int64_t atm_gettimeofday(atm_timeval_t *out){return atm_syscall1(ATM_SYS_GETTIMEOFDAY,(uint64_t)(uintptr_t)out);}
static inline int64_t atm_uname(atm_utsname_t *out){return atm_syscall1(ATM_SYS_UNAME,(uint64_t)(uintptr_t)out);}
static inline int64_t atm_nanosleep(const atm_clock_timespec_t *req,atm_clock_timespec_t *rem){return atm_syscall2(ATM_SYS_NANOSLEEP,(uint64_t)(uintptr_t)req,(uint64_t)(uintptr_t)rem);}
static inline int64_t atm_select(int nfds,atm_fdset_t *readfds,atm_fdset_t *writefds,atm_fdset_t *exceptfds,const atm_timeval_t *timeout){return atm_syscall5(ATM_SYS_SELECT,(uint64_t)nfds,(uint64_t)(uintptr_t)readfds,(uint64_t)(uintptr_t)writefds,(uint64_t)(uintptr_t)exceptfds,(uint64_t)(uintptr_t)timeout);}
static inline int64_t atm_socket(int domain,int type,int protocol){
    return atm_syscall3(ATM_SYS_SOCKET,(uint64_t)domain,(uint64_t)type,(uint64_t)protocol);
}
static inline int64_t atm_connect(int fd,const void *addr,uint64_t addrlen){
    return atm_syscall3(ATM_SYS_CONNECT,(uint64_t)fd,(uint64_t)(uintptr_t)addr,addrlen);
}
static inline int64_t atm_bind(int fd,const void *addr,uint64_t addrlen){
    return atm_syscall3(ATM_SYS_BIND,(uint64_t)fd,(uint64_t)(uintptr_t)addr,addrlen);
}
static inline int64_t atm_listen(int fd,int backlog){ return atm_syscall2(ATM_SYS_LISTEN,(uint64_t)fd,(uint64_t)backlog); }
static inline int64_t atm_accept(int fd,void *addr,uint64_t addrlen){
    return atm_syscall3(ATM_SYS_ACCEPT,(uint64_t)fd,(uint64_t)(uintptr_t)addr,addrlen);
}
static inline int64_t atm_send(int fd,const void *buf,uint64_t count,uint32_t flags){return atm_syscall4(ATM_SYS_SEND,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count,flags);}
static inline int64_t atm_recv(int fd,void *buf,uint64_t count,uint32_t flags){return atm_syscall4(ATM_SYS_RECV,(uint64_t)fd,(uint64_t)(uintptr_t)buf,count,flags);}
static inline uint64_t atm_brk(uint64_t requested){ return (uint64_t)atm_syscall1(ATM_SYS_BRK,requested); }
/* Restricted native exec: static ET_EXEC only, argv is NULL or argv[0] plus
 * terminator, and envp is NULL or an empty vector. Success does not return. */
static inline int64_t atm_execve(const char *path,char *const argv[],char *const envp[]){
    return atm_syscall3(ATM_SYS_EXECVE,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)argv,(uint64_t)(uintptr_t)envp);
}
static inline int64_t atm_waitpid(int pid,int *status,int options){return atm_syscall3(ATM_SYS_WAITPID,(uint64_t)(int64_t)pid,(uint64_t)(uintptr_t)status,(uint64_t)options);}
static inline int64_t atm_kill(int pid,int sig){return atm_syscall2(ATM_SYS_KILL,(uint64_t)(int64_t)pid,(uint64_t)(int64_t)sig);}
static inline uint64_t atm_getpid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETPID); }
static inline uint64_t atm_getppid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETPPID); }
static inline uint64_t atm_getuid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETUID); }
static inline uint64_t atm_getgid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETGID); }
static inline uint64_t atm_abi_info(void){ return (uint64_t)atm_syscall0(ATM_SYS_ABI_INFO); }
__attribute__((noreturn)) static inline void atm_exit(int status){
    (void)atm_syscall1(ATM_SYS_EXIT,(uint64_t)status);
    for(;;) __asm__ volatile("hlt");
}

#endif
