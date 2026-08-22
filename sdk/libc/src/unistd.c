/* ATMKoala native libc unistd wrappers — MIT licensed project code. */
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include "atm_native_abi.h"
#include "internal.h"

char *optarg;
int optind=1,opterr=1,optopt;
static int getopt_cursor=1,getopt_last_ind;

int getopt(int argc,char *const argv[],const char *optstring){
    const char *spec;char *arg;char option;int missing_colon;
    optarg=0;if(!argv||!optstring||argc<=0)return -1;
    if(optind<1){optind=1;getopt_cursor=1;}
    if(optind==1&&getopt_last_ind!=1)getopt_cursor=1;
    if(optind>=argc||!(arg=argv[optind])){getopt_last_ind=optind;return -1;}
    if(getopt_cursor==1){
        if(arg[0]!='-'||!arg[1]){getopt_last_ind=optind;return -1;}
        if(arg[1]=='-'&&!arg[2]){optind++;getopt_cursor=1;getopt_last_ind=optind;return -1;}
    }
    option=arg[getopt_cursor++];missing_colon=optstring[0]==':';
    spec=optstring+missing_colon;while(*spec&&*spec!=option)spec++;
    if(option==':'||!*spec){
        optopt=(unsigned char)option;
        if(!arg[getopt_cursor]){optind++;getopt_cursor=1;}
        getopt_last_ind=optind;return '?';
    }
    if(spec[1]==':'){
        if(arg[getopt_cursor]){optarg=&arg[getopt_cursor];optind++;getopt_cursor=1;}
        else if(optind+1<argc&&argv[optind+1]){optarg=argv[optind+1];optind+=2;getopt_cursor=1;}
        else {optopt=(unsigned char)option;optind++;getopt_cursor=1;getopt_last_ind=optind;return missing_colon?':':'?';}
    } else if(!arg[getopt_cursor]){optind++;getopt_cursor=1;}
    getopt_last_ind=optind;return (unsigned char)option;
}

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
ssize_t preadv(int fd,const struct iovec *iov,int iovcnt,off_t offset){if(offset<0)return (ssize_t)__atm_sysret(-ATM_EINVAL);return (ssize_t)__atm_sysret(atm_preadv(fd,(const atm_iovec_t *)iov,iovcnt,(uint64_t)offset));}
ssize_t pwritev(int fd,const struct iovec *iov,int iovcnt,off_t offset){if(offset<0)return (ssize_t)__atm_sysret(-ATM_EINVAL);return (ssize_t)__atm_sysret(atm_pwritev(fd,(const atm_iovec_t *)iov,iovcnt,(uint64_t)offset));}
int dup(int fd){return (int)__atm_sysret(atm_dup(fd));}
int dup2(int fd,int newfd){return (int)__atm_sysret(atm_dup2(fd,newfd));}
int dup3(int oldfd,int newfd,int flags){return (int)__atm_sysret(atm_dup3(oldfd,newfd,(uint32_t)flags));}
int pipe(int pipefd[2]){return (int)__atm_sysret(atm_pipe(pipefd));}
int pipe2(int pipefd[2],int flags){return (int)__atm_sysret(atm_pipe2(pipefd,(uint32_t)flags));}
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
int renameat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath){return (int)__atm_sysret(atm_renameat(olddirfd,oldpath,newdirfd,newpath));}
int symlink(const char *target,const char *linkpath){return (int)__atm_sysret(atm_symlink(target,linkpath));}
int symlinkat(const char *target,int newdirfd,const char *linkpath){return (int)__atm_sysret(atm_symlinkat(target,newdirfd,linkpath));}
ssize_t readlink(const char *path,char *buf,size_t size){return (ssize_t)__atm_sysret(atm_readlink(path,buf,(uint64_t)size));}
ssize_t readlinkat(int dirfd,const char *path,char *buf,size_t size){return (ssize_t)__atm_sysret(atm_readlinkat(dirfd,path,buf,(uint64_t)size));}
int linkat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath,int flags){return (int)__atm_sysret(atm_linkat(olddirfd,oldpath,newdirfd,newpath,(uint32_t)flags));}
int unlinkat(int dirfd,const char *path,int flags){return (int)__atm_sysret(atm_unlinkat(dirfd,path,(uint32_t)flags));}
int mkdirat(int dirfd,const char *path,mode_t mode){return (int)__atm_sysret(atm_mkdirat(dirfd,path,(uint32_t)mode));}
int access(const char *path,int mode){return (int)__atm_sysret(atm_access(path,mode));}
int faccessat(int dirfd,const char *path,int mode,int flags){return (int)__atm_sysret(atm_faccessat(dirfd,path,mode,flags));}
int chdir(const char *path){return (int)__atm_sysret(atm_chdir(path));}
int fchdir(int fd){return (int)__atm_sysret(atm_fchdir(fd));}
int chown(const char *path,uid_t owner,gid_t group){return (int)__atm_sysret(atm_chown(path,owner,group));}
int fchown(int fd,uid_t owner,gid_t group){return (int)__atm_sysret(atm_fchown(fd,owner,group));}
int fchmod(int fd,mode_t mode){return (int)__atm_sysret(atm_fchmod(fd,(uint32_t)mode));}
char *getcwd(char *buf,size_t size){int64_t rc=atm_getcwd(buf,(uint64_t)size);if(rc<0){(void)__atm_sysret(rc);return NULL;}return (char *)(uintptr_t)rc;}
int mkdir(const char *path,mode_t mode){return (int)__atm_sysret(atm_mkdir(path,mode));}
int rmdir(const char *path){return (int)__atm_sysret(atm_rmdir(path));}
int link(const char *oldpath,const char *newpath){return (int)__atm_sysret(atm_link(oldpath,newpath));}
int unlink(const char *path){return (int)__atm_sysret(atm_unlink(path));}
mode_t umask(mode_t mask){return (mode_t)atm_umask(mask);}
int isatty(int fd){return (int)__atm_sysret(atm_isatty(fd));}
int ttyname_r(int fd,char *buf,size_t size){
    static const char name[]="/dev/tty";
    if(!buf||size<sizeof(name))return ERANGE;
    if(!isatty(fd))return ENOTTY;
    memcpy(buf,name,sizeof(name));return 0;
}
char *ttyname(int fd){
    static char name[9];int e=ttyname_r(fd,name,sizeof(name));
    if(e){errno=e;return NULL;}return name;
}
long sysconf(int name){
    if(name==_SC_PAGESIZE || name==_SC_PAGE_SIZE)return 4096;
    if(name==_SC_CLK_TCK)return 100;
    return (long)__atm_sysret(-ATM_EINVAL);
}
size_t confstr(int name,char *buf,size_t len){
    static const char path[]="/syls/bin";size_t need=sizeof(path);
    if(name!=_CS_PATH){errno=ATM_EINVAL;return 0;}
    if(buf&&len){size_t copy=len-1;if(copy>need-1)copy=need-1;memcpy(buf,path,copy);buf[copy]=0;}
    return need;
}
int getpagesize(void){return 4096;}
int gethostname(char *name,size_t len){
    atm_utsname_t identity;size_t need;int64_t rc;
    if(!name)return (int)__atm_sysret(-ATM_EFAULT);
    rc=atm_uname(&identity);if(rc<0)return (int)__atm_sysret(rc);
    need=strlen(identity.nodename)+1;
    if(!len||len<need)return (int)__atm_sysret(-ATM_EINVAL);
    memcpy(name,identity.nodename,need);return 0;
}
/* No supplementary credentials are attached to a task yet. A non-negative
 * capacity never needs dereferencing because the truthful result is empty. */
int getgroups(int size,gid_t list[]){(void)list;return size<0?(int)__atm_sysret(-ATM_EINVAL):0;}
int setgroups(size_t size,const gid_t list[]){(void)size;(void)list;return (int)__atm_sysret(-ATM_EPERM);}
int initgroups(const char *user,gid_t group){(void)user;(void)group;return (int)__atm_sysret(-ATM_EPERM);}
int getrusage(int who,struct rusage *usage){
    if(!usage)return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_getrusage(who,(atm_rusage_t *)usage));
}
int getrlimit(int resource,struct rlimit *limit){
    if(!limit)return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_getrlimit(resource,(atm_rlimit_t *)limit));
}
clock_t times(struct tms *buf){return (clock_t)__atm_sysret(atm_times((atm_tms_t *)buf));}
pid_t getpid(void){ return (pid_t)atm_getpid(); }
pid_t getppid(void){ return (pid_t)atm_getppid(); }
pid_t getpgid(pid_t pid){return (pid_t)__atm_sysret(atm_getpgid(pid));}
int setpgid(pid_t pid,pid_t pgid){return (int)__atm_sysret(atm_setpgid(pid,pgid));}
pid_t getpgrp(void){return getpgid(0);}
pid_t getsid(pid_t pid){return (pid_t)__atm_sysret(atm_getsid(pid));}
pid_t setsid(void){return (pid_t)__atm_sysret(atm_setsid());}
uid_t getuid(void){return (uid_t)atm_getuid();}
gid_t getgid(void){return (gid_t)atm_getgid();}
uid_t geteuid(void){return (uid_t)atm_geteuid();}
gid_t getegid(void){return (gid_t)atm_getegid();}
int getresuid(uid_t *real,uid_t *effective,uid_t *saved){return (int)__atm_sysret(atm_getresuid(real,effective,saved));}
int getresgid(gid_t *real,gid_t *effective,gid_t *saved){return (int)__atm_sysret(atm_getresgid(real,effective,saved));}
pid_t waitpid(pid_t pid,int *status,int options){return (pid_t)__atm_sysret(atm_waitpid((int)pid,status,options));}
pid_t wait(int *status){return waitpid((pid_t)-1,status,0);}
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
