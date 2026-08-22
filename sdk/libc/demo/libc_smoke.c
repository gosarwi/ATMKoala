#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/select.h>
#include "atm_gui.h"
#include "atm_native_abi.h"

static int cmp_int(const void *a,const void *b){
    int x=*(const int *)a,y=*(const int *)b;
    return (x>y)-(x<y);
}

int main(int argc,char **argv,char **envp){
    uint64_t *auxv=(uint64_t *)(envp?envp+1:0);
    if(argc!=1 || !argv || !argv[0] || strcmp(argv[0],"libc-smoke") || argv[1] ||
       !envp || envp[0] || environ!=envp || getenv("ATMKOALA_TEST") || getenv("") || getenv("A=B") || !auxv || auxv[0]!=6 || auxv[1]!=4096 ||
       auxv[2]!=9 || auxv[3]<0x40000000ULL || auxv[4]!=0 || auxv[5]!=0 ||
       atm_abi_info()!=22 || getpagesize()!=4096 || sysconf(_SC_PAGESIZE)!=4096 || sysconf(_SC_PAGE_SIZE)!=4096 ||
       sysconf(_SC_CLK_TCK)!=100 || (errno=0,sysconf(-1)!=-1) || errno!=EINVAL) return 33;
    char ttybuf[9];
    if(!isatty(0) || ttyname_r(0,ttybuf,sizeof(ttybuf))!=0 || strcmp(ttybuf,"/dev/tty") || !ttyname(1) ||
       ttyname_r(0,ttybuf,sizeof(ttybuf)-1)!=ERANGE || ttyname_r(31,ttybuf,sizeof(ttybuf))!=ENOTTY ||
       (errno=0,ttyname(31)!=NULL) || errno!=ENOTTY) return 33;
    char g0[]="tool",g1[]="-ab",g2[]="-c",g3[]="value",g4[]="operand";
    char *gargv[]={g0,g1,g2,g3,g4,0}; optind=1;opterr=0;
    if(getopt(5,gargv,"abc:")!='a' || optarg || optind!=1 || getopt(5,gargv,"abc:")!='b' || optind!=2 ||
       getopt(5,gargv,"abc:")!='c' || !optarg || strcmp(optarg,"value") || optind!=4 || getopt(5,gargv,"abc:")!=-1 || optind!=4) return 51;
    char u0[]="tool",u1[]="-z";char *uargv[]={u0,u1,0};optind=1;
    if(getopt(2,uargv,"a")!='?' || optopt!='z' || optind!=2) return 51;
    char e0[]="tool",e1[]="-c";char *eargv[]={e0,e1,0};optind=1;
    if(getopt(2,eargv,":c:")!=':' || optopt!='c' || optind!=2) return 51;
    char d0[]="tool",d1[]="-a",d2[]="--",d3[]="-b";char *dargv[]={d0,d1,d2,d3,0};optind=1;
    if(getopt(4,dargv,"ab")!='a' || getopt(4,dargv,"ab")!=-1 || optind!=3) return 51;
    char n0[]="tool",n1[]="operand";char *nargv[]={n0,n1,0};optind=1;
    if(getopt(2,nargv,"a")!=-1 || optind!=1) return 51;
    struct rusage usage;
    if(getrusage(RUSAGE_SELF,&usage)<0 || usage.ru_utime.tv_sec<0 || usage.ru_utime.tv_usec<0 || usage.ru_utime.tv_usec>=1000000 ||
       usage.ru_stime.tv_sec!=0 || usage.ru_stime.tv_usec!=0 || usage.ru_maxrss<=0 || usage.ru_ixrss!=0 || usage.ru_nvcsw<0 ||
       (errno=0,getrusage(1,&usage)!=-1) || errno!=EINVAL || (errno=0,getrusage(RUSAGE_SELF,0)!=-1) || errno!=EFAULT) return 33;
    struct tms cpu_times;clock_t uptime_ticks=times(&cpu_times);
    if(uptime_ticks<0 || cpu_times.tms_utime<0 || cpu_times.tms_stime!=0 || cpu_times.tms_cutime!=0 || cpu_times.tms_cstime!=0 || times(0)<uptime_ticks) return 33;
    struct rlimit limits;
    if(getrlimit(RLIMIT_NOFILE,&limits)<0 || limits.rlim_cur!=32 || limits.rlim_max!=32 ||
       getrlimit(RLIMIT_STACK,&limits)<0 || limits.rlim_cur!=4096 || limits.rlim_max!=4096 ||
       getrlimit(RLIMIT_AS,&limits)<0 || limits.rlim_cur!=0x8000000ULL || limits.rlim_max!=0x8000000ULL ||
       (errno=0,getrlimit(0,&limits)!=-1) || errno!=EINVAL || (errno=0,getrlimit(RLIMIT_NOFILE,0)!=-1) || errno!=EFAULT) return 33;
    static const char expected[]="[libc] static CRT + musl subset + POSIX I/O = OK\n";
    size_t n=strlen(expected);
    char *buffer=(char *)calloc(n+1,1);
    if(!buffer) return 1;
    strcpy(buffer,expected);
    if(strlen(buffer)!=n || strcmp(buffer,expected)!=0) return 2;
    char *grown=(char *)realloc(buffer,n+32);
    if(!grown) return 3;

    char words[]="Alpha,beta;GAMMA"; char *save=0;
    char *one=strtok_r(words,",;",&save),*two=strtok_r(0,",;",&save),*three=strtok_r(0,",;",&save);
    if(!one||!two||!three||strcmp(one,"Alpha")||strcmp(two,"beta")||strcasecmp(three,"gamma")) return 4;
    if(strspn("abc123","abc")!=3 || strcspn("abc123","123")!=3 || !strstr("atmkoala","koal") || *strrchr("a/b/c",'/')!='/') return 5;
    char *dupe=strndup("native-libc",6);
    if(!dupe || strcmp(dupe,"native")) return 6;
    free(dupe);
    if(!isalpha('A')||!isdigit('7')||!isxdigit('f')||!isspace('\t')||tolower('Q')!='q'||toupper('m')!='M') return 7;

    char *end=0; errno=0;
    if(strtol(" -0x2aZ",&end,0)!=-42 || !end || *end!='Z' || errno) return 8;
    errno=0; if(strtoull("18446744073709551616",&end,10)!=0xffffffffffffffffULL || errno!=ERANGE) return 9;
    int sorted[]={1,4,9,16,25}; int wanted=16; int *found=(int *)bsearch(&wanted,sorted,5,sizeof(sorted[0]),cmp_int);
    if(!found || *found!=16 || div(17,5).rem!=2 || lldiv(-17,5).quot!=-3) return 10;

    atm_gui_runtime_info_t gui;
    if(atm_gui_runtime_info(&gui)<0 || gui.abi_version!=ATM_GUI_ABI_VERSION || gui.capabilities!=0) return 11;
    if(atm_gui_window_create(NULL,NULL)!=-1 || errno!=ENOSYS) return 12;
    stat_t st;
    if(fstat(STDOUT_FILENO,&st)<0) return 13;
    int sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); char socket_byte=0; struct pollfd socket_poll={sock,POLLOUT,0};
    if(sock<3 || poll(&socket_poll,1,0)!=1 || socket_poll.revents!=POLLHUP || send(sock,"x",1,0)>=0 || recv(sock,&socket_byte,1,MSG_DONTWAIT)>=0 ||
       (errno=0,send(sock,"x",1,MSG_DONTWAIT)!=-1) || errno!=EINVAL || close(sock)<0) return 14;
    int child_status=0;
    if(waitpid(-1,&child_status,WNOHANG)!=-1 || errno!=ECHILD || (errno=0,waitpid(-1,&child_status,0)!=-1) || errno!=ECHILD ||
       (errno=0,wait(&child_status)!=-1) || errno!=ECHILD) return 29;
    int pipefd[2]; char pipebuf[5]={0}; struct pollfd pollfd;
    if(pipe(pipefd)<0 || pipefd[0]<3 || pipefd[1]<3) return 30;
    pollfd.fd=pipefd[0];pollfd.events=POLLIN;pollfd.revents=0;
    if(poll(&pollfd,1,0)!=0 || pollfd.revents || poll(&pollfd,1,1)!=0 || pollfd.revents ||
       (errno=0,poll(&pollfd,1,-1)!=-1) || errno!=EINVAL) return 30;
    fd_set timed_readset; struct timeval finite_timeout={0,1}; FD_ZERO(&timed_readset); FD_SET(pipefd[0],&timed_readset);
    if(select(pipefd[0]+1,&timed_readset,0,0,&finite_timeout)!=0 || FD_ISSET(pipefd[0],&timed_readset)) return 30;
    pollfd.fd=pipefd[1];pollfd.events=POLLOUT;pollfd.revents=0;
    if(poll(&pollfd,1,0)!=1 || pollfd.revents!=POLLOUT || fcntl(pipefd[0],F_GETFL)!=(int)O_RDONLY ||
       fcntl(pipefd[0],F_GETFD)!=0 || fcntl(pipefd[0],F_SETFD,FD_CLOEXEC)<0 ||
       fcntl(pipefd[0],F_GETFD)!=FD_CLOEXEC || fcntl(pipefd[0],F_SETFL,O_NONBLOCK)<0 ||
       fcntl(pipefd[0],F_GETFL)!=(int)(O_RDONLY|O_NONBLOCK) ||
       ((errno=0),read(pipefd[0],pipebuf,1)!=-1) || errno!=EAGAIN ||
       fcntl(pipefd[0],F_SETFL,0)<0 || write(pipefd[1],"pipe",4)!=4) return 30;
    pollfd.fd=pipefd[0];pollfd.events=POLLIN;pollfd.revents=0;
    if(poll(&pollfd,1,0)!=1 || pollfd.revents!=POLLIN) return 30;
    fd_set readset; struct timeval zero_timeout={0,0}; FD_ZERO(&readset); FD_SET(pipefd[0],&readset);
    if(select(pipefd[0]+1,&readset,0,0,&zero_timeout)!=1 || !FD_ISSET(pipefd[0],&readset)) return 30;
    int pipe_copy=fcntl(pipefd[0],F_DUPFD,12); int pipe_cloexec=fcntl(pipefd[0],F_DUPFD_CLOEXEC,13);
    if(pipe_copy!=12 || fcntl(pipe_copy,F_GETFD)!=0 || pipe_cloexec!=13 || fcntl(pipe_cloexec,F_GETFD)!=FD_CLOEXEC || close(pipe_copy)<0 || close(pipe_cloexec)<0) return 30;
    int pipe2fd[2];
    if(pipe2(pipe2fd,O_CLOEXEC|O_NONBLOCK)<0) return 34;
    if(fcntl(pipe2fd[0],F_GETFD)!=FD_CLOEXEC || fcntl(pipe2fd[0],F_GETFL)!=(int)(O_RDONLY|O_NONBLOCK)) return 35;
    if(dup3(pipe2fd[1],14,O_CLOEXEC)!=14 || fcntl(14,F_GETFD)!=FD_CLOEXEC) return 36;
    if((errno=0,dup3(14,14,0)!=-1) || errno!=EINVAL || (errno=0,pipe2(pipe2fd,1)!=-1) || errno!=EINVAL) return 37;
    if(close(14)<0 || close(pipe2fd[0])<0 || close(pipe2fd[1])<0) return 38;
    if(close(pipefd[1])<0 ||
       read(pipefd[0],pipebuf,4)!=4 || memcmp(pipebuf,"pipe",4) || read(pipefd[0],pipebuf,1)!=0 || close(pipefd[0])<0) return 30;

    static char saved_cwd[128],cwd[128],readback[4],linkbuf[16];
    memset(saved_cwd,0,sizeof(saved_cwd));memset(cwd,0,sizeof(cwd));memset(readback,0,sizeof(readback));memset(linkbuf,0,sizeof(linkbuf));
    const char *dir="/tmp/.atm-libc-posix";
    const char *file="/tmp/.atm-libc-posix/io";
    const char *movedfile="/tmp/.atm-libc-posix/moved";
    const char *linkfile="/tmp/.atm-libc-posix/alias";
    const char *hardfile="/tmp/.atm-libc-posix/hard";
    const char *vecfile="/tmp/.atm-libc-posix/vec";
    if(!getcwd(saved_cwd,sizeof(saved_cwd))) return 15;
    mode_t oldmask=umask(0077);
    (void)unlink(file); (void)unlink(movedfile); (void)unlink(linkfile); (void)unlink(hardfile); (void)unlink(vecfile); (void)rmdir(dir);
    if(mkdir(dir,0777)<0 || chdir(dir)<0 || !getcwd(cwd,sizeof(cwd)) || strcmp(cwd,dir)) return 16;
    int fd=open("io",O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC,0666);
    if(fd<3 || access("io",F_OK)<0 || access("io",R_OK|W_OK)<0 || faccessat(AT_FDCWD,"io",R_OK|W_OK,0)<0 ||
       (errno=0,faccessat(AT_FDCWD,"io",R_OK,1)!=-1) || errno!=EINVAL || (errno=0,faccessat(fd,"io",R_OK,0)!=-1) || errno!=EINVAL ||
       fcntl(fd,F_GETFD)!=FD_CLOEXEC || fcntl(fd,F_GETFL)!=(int)O_RDWR ||
       fcntl(fd,F_SETFL,O_NONBLOCK)<0 || fcntl(fd,F_GETFL)!=(int)(O_RDWR|O_NONBLOCK) || fcntl(fd,F_SETFL,0)<0 ||
       fchmod(fd,0600)<0 || fstat(fd,&st)<0 || (st.st_mode&0777)!=0600 || fchown(fd,0,0)<0 ||
       (errno=0,fchmod(-1,0600)!=-1) || errno!=EBADF || (errno=0,fchown(-1,0,0)!=-1) || errno!=EBADF) return 17;
    int cwdfd=open(".",O_RDONLY|O_DIRECTORY);
    if(cwdfd<3 || fchdir(cwdfd)<0 || !getcwd(cwd,sizeof(cwd)) || strcmp(cwd,dir) ||
       (errno=0,fchdir(fd)!=-1) || errno!=ENOTDIR || close(cwdfd)<0) return 17;
    (void)unlinkat(AT_FDCWD,"athard",0);(void)unlinkat(AT_FDCWD,"atlink",0);(void)unlinkat(AT_FDCWD,"atdir",AT_REMOVEDIR);
    if(mkdirat(AT_FDCWD,"atdir",0700)<0 || linkat(AT_FDCWD,"io",AT_FDCWD,"athard",0)<0 ||
       symlinkat("io",AT_FDCWD,"atlink")<0 || readlinkat(AT_FDCWD,"atlink",linkbuf,sizeof(linkbuf))!=2 || memcmp(linkbuf,"io",2) ||
       renameat(AT_FDCWD,"io",AT_FDCWD,"io-at")<0 || renameat(AT_FDCWD,"io-at",AT_FDCWD,"io")<0 ||
       unlinkat(AT_FDCWD,"athard",0)<0 || unlinkat(AT_FDCWD,"atlink",0)<0 || unlinkat(AT_FDCWD,"atdir",AT_REMOVEDIR)<0 ||
       (errno=0,mkdirat(3,"atbad",0700)!=-1) || errno!=EINVAL) return 17;
    int atfd=openat(AT_FDCWD,"io",O_RDONLY);
    if(atfd<3 || fstatat(AT_FDCWD,"io",&st,0)<0 || close(atfd)<0 ||
       (errno=0,openat(3,"io",O_RDONLY)>=0) || errno!=EINVAL) return 17;
    if(pwrite(fd,"abc",3,0)!=3 || pread(fd,readback,3,0)!=3 || memcmp(readback,"abc",3)) return 18;
    int copy=dup(fd); if(copy<3 || ftruncate(fd,2)<0 || fsync(fd)<0 || fdatasync(copy)<0) return 19;
    if(pread(copy,readback,2,0)!=2 || memcmp(readback,"ab",2)) return 20;
    if(dup2(fd,10)!=10 || close(10)<0 || close(copy)<0 || close(fd)<0) return 21;
    if(stat("io",&st)<0 || st.st_size!=2 || rename("io","moved")<0 || access("io",F_OK)==0 || truncate("moved",1)<0 || chmod("moved",0640)<0 || chown("moved",getuid(),getgid())<0 || lstat("moved",&st)<0 || st.st_size!=1 || (st.st_mode&0777)!=0640) return 22;
    if(symlink("moved","alias")<0 || readlink("alias",linkbuf,sizeof(linkbuf))!=5 || memcmp(linkbuf,"moved",5)) return 23;
    if(link("moved","hard")<0 || stat("hard",&st)<0 || st.st_nlink<2) return 24;
    int vecfd=open("vec",O_RDWR|O_CREAT|O_TRUNC,0600);
    struct iovec outv[2]={{(void *)"v",1},{(void *)"io",2}};
    struct iovec inv[2]={{readback,1},{readback+1,2}};
    if(vecfd<3 || writev(vecfd,outv,2)!=3 || lseek(vecfd,0,SEEK_SET)<0 || readv(vecfd,inv,2)!=3 || memcmp(readback,"vio",3) ||
       pwritev(vecfd,outv,2,0)!=3 || preadv(vecfd,inv,2,0)!=3 || memcmp(readback,"vio",3) ||
       lseek(vecfd,0,SEEK_CUR)!=3 || (errno=0,preadv(vecfd,inv,2,-1)!=-1) || errno!=EINVAL || close(vecfd)<0) return 25;
    if(getpgid(0)!=getpid() || getpgrp()!=getpid() || getsid(0)!=getpid() || setpgid(0,0)<0 ||
       (errno=0,setsid()!=-1) || errno!=EINVAL || (errno=0,getpgid(-1)!=-1) || errno!=EINVAL ||
       (errno=0,getsid(-1)!=-1) || errno!=EINVAL) return 39;
    if(kill(getpid(),0)<0 || (errno=0,kill(0,0)!=-1) || errno!=EINVAL ||
       (errno=0,kill(0x7fffffff,0)!=-1) || errno!=ESRCH) return 46;
    gid_t groups[1];
    if(getgroups(0,0)!=0 || getgroups(1,groups)!=0 ||
       (errno=0,getgroups(-1,groups)!=-1) || errno!=EINVAL ||
       (errno=0,setgroups(0,0)!=-1) || errno!=EPERM ||
       (errno=0,initgroups("root",0)!=-1) || errno!=EPERM) return 48;
    uid_t ruid,euid,suid;gid_t rgid,egid,sgid;
    if(!isatty(STDIN_FILENO) || getuid()>(uid_t)0xffffffffu || getgid()>(gid_t)0xffffffffu || geteuid()!=getuid() || getegid()!=getgid() || getresuid(&ruid,&euid,&suid)<0 || ruid!=getuid() || euid!=geteuid() || suid!=getuid() || getresgid(&rgid,&egid,&sgid)<0 || rgid!=getgid() || egid!=getegid() || sgid!=getgid() || getresuid(0,&euid,&suid)>=0) return 26;
    struct timespec mono,real; struct timeval wall;
    if(clock_gettime(CLOCK_MONOTONIC,&mono)<0 || mono.tv_sec<0 || mono.tv_nsec<0 || mono.tv_nsec>=1000000000LL) return 32;
    struct timespec resolution;
    if(clock_getres(CLOCK_MONOTONIC,&resolution)<0 || resolution.tv_sec!=0 || resolution.tv_nsec!=10000000LL ||
       clock_getres(-1,&resolution)>=0) return 32;
    struct timespec before_sleep,after_sleep,sleep_req={0,10000000LL},sleep_rem={-1,-1},bad_sleep={0,1000000000LL};
    if(clock_gettime(CLOCK_MONOTONIC,&before_sleep)<0 || nanosleep(&bad_sleep,0)>=0 ||
       nanosleep(&sleep_req,&sleep_rem)<0 || sleep_rem.tv_sec!=0 || sleep_rem.tv_nsec!=0 ||
       clock_gettime(CLOCK_MONOTONIC,&after_sleep)<0 ||
       ((after_sleep.tv_sec-before_sleep.tv_sec)*1000000000LL+after_sleep.tv_nsec-before_sleep.tv_nsec)<10000000LL) return 32;
    struct utsname uts;
    if(uname(&uts)<0 || strcmp(uts.sysname,"ATMKoala") || strcmp(uts.machine,"x86_64") || uname(0)>=0) return 32;
    char hostname[9],hostname_short[8];
    if(gethostname(hostname,sizeof(hostname))<0 || strcmp(hostname,"atmkoala") ||
       (errno=0,gethostname(hostname_short,sizeof(hostname_short))!=-1) || errno!=EINVAL ||
       (errno=0,gethostname(0,sizeof(hostname))!=-1) || errno!=EFAULT) return 47;
    char confpath[16],confshort[4];
    if(confstr(_CS_PATH,0,0)!=10 || confstr(_CS_PATH,confpath,sizeof(confpath))!=10 || strcmp(confpath,"/syls/bin") ||
       confstr(_CS_PATH,confshort,sizeof(confshort))!=10 || strcmp(confshort,"/sy") ||
       (errno=0,confstr(-1,confpath,sizeof(confpath))!=0) || errno!=EINVAL) return 49;
    if(clock_gettime(CLOCK_REALTIME,&real)<0 || gettimeofday(&wall,0)<0) return 32;
    time_t now=time(0); if(now<wall.tv_sec || now>wall.tv_sec+1) return 32;
    DIR *dirp=opendir("."); int found_moved=0;
    if(!dirp) return 31;
    for(int i=0;i<64;i++){dirent_t *entry=readdir(dirp);if(!entry)break;if(!strcmp(entry->d_name,"moved"))found_moved=1;}
    if(closedir(dirp)<0 || !found_moved) return 31;
    if(chdir("/")<0 || unlink(linkfile)<0 || unlink(hardfile)<0 || unlink(movedfile)<0 || unlink(vecfile)<0 || rmdir(dir)<0) return 27;
    (void)umask(oldmask);

    if(write(STDOUT_FILENO,grown,n)!=(ssize_t)n) return 28;
    free(grown);
    return 42;
}
