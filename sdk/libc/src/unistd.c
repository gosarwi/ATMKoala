/* ATMKoala native libc unistd wrappers — MIT licensed project code. */
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "atm_native_abi.h"
#include "internal.h"

ssize_t read(int fd,void *buf,size_t count){
    return (ssize_t)__atm_sysret(atm_read(fd,buf,(uint64_t)count));
}

ssize_t write(int fd,const void *buf,size_t count){
    return (ssize_t)__atm_sysret(atm_write(fd,buf,(uint64_t)count));
}

int close(int fd){ return (int)__atm_sysret(atm_close(fd)); }
off_t lseek(int fd,off_t offset,int whence){
    return (off_t)__atm_sysret(atm_lseek(fd,(int64_t)offset,whence));
}
int fstat(int fd,stat_t *st){ return (int)__atm_sysret(atm_fstat(fd,st)); }

int open(const char *path,int flags,...){
    /* v0.9 VFS ignores creation mode; accept the variadic POSIX shape so
     * ordinary source can be compiled without a special ATM-only signature. */
    return (int)__atm_sysret(atm_open(path,(uint32_t)flags,0));
}

int getpid(void){ return (int)atm_getpid(); }
int getppid(void){ return (int)atm_getppid(); }
void _exit(int status){ atm_exit(status); }
