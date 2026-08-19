#ifndef ATM_NATIVE_ABI_H
#define ATM_NATIVE_ABI_H

/* ATM Native App ABI v1 public header.
 * Include this header from a freestanding static x86-64 application linked for
 * ATMKoala. It is intentionally not a Linux libc replacement. Calls return a
 * non-negative result or a negative ATM errno-style value; applications may
 * translate errors into their own errno storage if desired. */

#include <stdint.h>

#define ATM_NATIVE_ABI_MAJOR 1u
#define ATM_NATIVE_ABI_MINOR 0u

#define ATM_SYS_READ       0u
#define ATM_SYS_WRITE      1u
#define ATM_SYS_OPEN       2u
#define ATM_SYS_CLOSE      3u
#define ATM_SYS_FSTAT      5u
#define ATM_SYS_LSEEK      8u
#define ATM_SYS_BRK       12u
#define ATM_SYS_EXIT      60u
#define ATM_SYS_WAITPID   61u
#define ATM_SYS_KILL      62u
#define ATM_SYS_GETPID    39u
#define ATM_SYS_GETPPID  110u
#define ATM_SYS_GETUID   102u
#define ATM_SYS_GETGID   104u
#define ATM_SYS_GETTID   186u
#define ATM_SYS_ABI_INFO 0xA700u
#define ATM_SYS_SOCKET   0xA710u
#define ATM_SYS_CONNECT  0xA711u
#define ATM_SYS_BIND     0xA712u
#define ATM_SYS_LISTEN   0xA713u
#define ATM_SYS_ACCEPT   0xA714u

#define ATM_EFAULT 14
#define ATM_ENOMEM 12
#define ATM_EINVAL 22
#define ATM_ENOSYS 38

#define ATM_O_RDONLY 0x0000u
#define ATM_O_WRONLY 0x0001u
#define ATM_O_RDWR   0x0002u
#define ATM_O_CREAT  0x0040u
#define ATM_O_TRUNC  0x0200u
#define ATM_SEEK_SET 0
#define ATM_SEEK_CUR 1
#define ATM_SEEK_END 2

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

static inline int64_t atm_open(const char *path,uint32_t flags,uint32_t mode){
    return atm_syscall3(ATM_SYS_OPEN,(uint64_t)(uintptr_t)path,flags,mode);
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
static inline uint64_t atm_brk(uint64_t requested){ return (uint64_t)atm_syscall1(ATM_SYS_BRK,requested); }
static inline uint64_t atm_getpid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETPID); }
static inline uint64_t atm_getppid(void){ return (uint64_t)atm_syscall0(ATM_SYS_GETPPID); }
static inline uint64_t atm_abi_info(void){ return (uint64_t)atm_syscall0(ATM_SYS_ABI_INFO); }
__attribute__((noreturn)) static inline void atm_exit(int status){
    (void)atm_syscall1(ATM_SYS_EXIT,(uint64_t)status);
    for(;;) __asm__ volatile("hlt");
}

#endif
