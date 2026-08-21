/* ATMKoala native libc unistd wrappers — MIT licensed project code. */
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include "atm_native_abi.h"
#include "internal.h"

ssize_t read(int fd,void *buf,size_t count){
    return (ssize_t)__atm_sysret(atm_read(fd,buf,(uint64_t)count));
}

ssize_t write(int fd,const void *buf,size_t count){
    return (ssize_t)__atm_sysret(atm_write(fd,buf,(uint64_t)count));
}

int close(int fd){ return (int)__atm_sysret(atm_close(fd)); }
ssize_t pread(int fd,void *buf,size_t count,off_t offset){return (ssize_t)__atm_sysret(atm_pread(fd,buf,(uint64_t)count,(uint64_t)offset));}
ssize_t pwrite(int fd,const void *buf,size_t count,off_t offset){return (ssize_t)__atm_sysret(atm_pwrite(fd,buf,(uint64_t)count,(uint64_t)offset));}
ssize_t readv(int fd,const struct iovec *iov,int iovcnt){return (ssize_t)__atm_sysret(atm_readv(fd,(const atm_iovec_t *)iov,iovcnt));}
ssize_t writev(int fd,const struct iovec *iov,int iovcnt){return (ssize_t)__atm_sysret(atm_writev(fd,(const atm_iovec_t *)iov,iovcnt));}
int dup(int fd){return (int)__atm_sysret(atm_dup(fd));}
int dup2(int fd,int newfd){return (int)__atm_sysret(atm_dup2(fd,newfd));}
int pipe(int pipefd[2]){return (int)__atm_sysret(atm_pipe(pipefd));}
off_t lseek(int fd,off_t offset,int whence){
    return (off_t)__atm_sysret(atm_lseek(fd,(int64_t)offset,whence));
}
int fsync(int fd){return (int)__atm_sysret(atm_fsync(fd));}
int fdatasync(int fd){return (int)__atm_sysret(atm_fdatasync(fd));}
int ftruncate(int fd,off_t length){if(length<0)return (int)__atm_sysret(-ATM_EINVAL);return (int)__atm_sysret(atm_ftruncate(fd,(uint64_t)length));}
int truncate(const char *path,off_t length){if(length<0)return (int)__atm_sysret(-ATM_EINVAL);return (int)__atm_sysret(atm_truncate(path,(uint64_t)length));}
int stat(const char *path,stat_t *st){return (int)__atm_sysret(atm_stat(path,st));}
int lstat(const char *path,stat_t *st){return (int)__atm_sysret(atm_lstat(path,st));}
int chmod(const char *path,mode_t mode){return (int)__atm_sysret(atm_chmod(path,mode));}
int fstat(int fd,stat_t *st){ return (int)__atm_sysret(atm_fstat(fd,st)); }
int fstatat(int dirfd,const char *path,stat_t *st,int flags){return (int)__atm_sysret(atm_fstatat(dirfd,path,st,(uint32_t)flags));}

int open(const char *path,int flags,...){
    uint32_t mode=0;
    if(flags&O_CREAT){
        __builtin_va_list ap;__builtin_va_start(ap,flags);
        mode=(uint32_t)__builtin_va_arg(ap,unsigned int);
        __builtin_va_end(ap);
    }
    return (int)__atm_sysret(atm_open(path,(uint32_t)flags,mode));
}
int openat(int dirfd,const char *path,int flags,...){
    uint32_t mode=0;
    if(flags&O_CREAT){
        __builtin_va_list ap;__builtin_va_start(ap,flags);
        mode=(uint32_t)__builtin_va_arg(ap,unsigned int);
        __builtin_va_end(ap);
    }
    return (int)__atm_sysret(atm_openat(dirfd,path,(uint32_t)flags,mode));
}

int rename(const char *oldpath,const char *newpath){return (int)__atm_sysret(atm_rename(oldpath,newpath));}
int symlink(const char *target,const char *linkpath){return (int)__atm_sysret(atm_symlink(target,linkpath));}
ssize_t readlink(const char *path,char *buf,size_t size){return (ssize_t)__atm_sysret(atm_readlink(path,buf,(uint64_t)size));}
int access(const char *path,int mode){return (int)__atm_sysret(atm_access(path,mode));}
int chdir(const char *path){return (int)__atm_sysret(atm_chdir(path));}
char *getcwd(char *buf,size_t size){int64_t rc=atm_getcwd(buf,(uint64_t)size);if(rc<0){(void)__atm_sysret(rc);return NULL;}return (char *)(uintptr_t)rc;}
int mkdir(const char *path,mode_t mode){return (int)__atm_sysret(atm_mkdir(path,mode));}
int rmdir(const char *path){return (int)__atm_sysret(atm_rmdir(path));}
int link(const char *oldpath,const char *newpath){return (int)__atm_sysret(atm_link(oldpath,newpath));}
int unlink(const char *path){return (int)__atm_sysret(atm_unlink(path));}
mode_t umask(mode_t mask){return (mode_t)atm_umask(mask);}
int isatty(int fd){return (int)__atm_sysret(atm_isatty(fd));}
pid_t getpid(void){ return (pid_t)atm_getpid(); }
pid_t getppid(void){ return (pid_t)atm_getppid(); }
uid_t getuid(void){return (uid_t)atm_getuid();}
gid_t getgid(void){return (gid_t)atm_getgid();}
pid_t waitpid(pid_t pid,int *status,int options){return (pid_t)__atm_sysret(atm_waitpid((int)pid,status,options));}
unsigned int sleep(unsigned int seconds){
    struct timespec req={(time_t)seconds,0};
    return nanosleep(&req,0)<0?seconds:0;
}
int usleep(useconds_t usec){
    struct timespec req={(time_t)(usec/1000000u),(int64_t)(usec%1000000u)*1000LL};
    return nanosleep(&req,0);
}
int kill(pid_t pid,int sig){return (int)__atm_sysret(atm_kill((int)pid,sig));}
int execve(const char *path,char *const argv[],char *const envp[]){
    return (int)__atm_sysret(atm_execve(path,argv,envp));
}
void _exit(int status){ atm_exit(status); }
