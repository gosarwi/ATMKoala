#ifndef ATM_LIBC_UNISTD_H
#define ATM_LIBC_UNISTD_H

#include <stddef.h>
#include <stdint.h>

/* The SDK headers are self-contained for the initial static runtime. */
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t useconds_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
/* Supported `sysconf` selectors in the static runtime. */
#define _SC_CLK_TCK  2

/* `confstr` exposes only the fixed native static-program search path. */
#define _CS_PATH 0

#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE

ssize_t read(int fd,void *buf,size_t count);
ssize_t write(int fd,const void *buf,size_t count);
ssize_t pread(int fd,void *buf,size_t count,off_t offset);
ssize_t pwrite(int fd,const void *buf,size_t count,off_t offset);
int     close(int fd);
int     dup(int fd);
int     dup2(int fd,int newfd);
/* Flags are limited to O_CLOEXEC; oldfd==newfd is rejected. */
int     dup3(int oldfd,int newfd,int flags);
int     pipe(int pipefd[2]);
/* Flags are limited to O_CLOEXEC and O_NONBLOCK. */
int     pipe2(int pipefd[2],int flags);
unsigned int sleep(unsigned int seconds);
int     usleep(useconds_t usec);
off_t   lseek(int fd,off_t offset,int whence);
int     fsync(int fd);
int     fdatasync(int fd);
int     ftruncate(int fd,off_t length);
int     truncate(const char *path,off_t length);
int     rename(const char *oldpath,const char *newpath);
int     symlink(const char *target,const char *linkpath);
ssize_t readlink(const char *path,char *buf,size_t size);
/* Each at-style namespace call currently accepts only AT_FDCWD. */
int     renameat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath);
int     linkat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath,int flags);
int     symlinkat(const char *target,int newdirfd,const char *linkpath);
ssize_t readlinkat(int dirfd,const char *path,char *buf,size_t size);
int     unlinkat(int dirfd,const char *path,int flags);
int     access(const char *path,int mode);
/* This ABI accepts only AT_FDCWD and flags=0. */
int     faccessat(int dirfd,const char *path,int mode,int flags);
int     chdir(const char *path);
/* Changes CWD through an open VFS-backed directory descriptor. */
int     fchdir(int fd);
int     chown(const char *path,uid_t owner,gid_t group);
int     fchown(int fd,uid_t owner,gid_t group);
char   *getcwd(char *buf,size_t size);
int     mkdir(const char *path,mode_t mode);
int     rmdir(const char *path);
int     link(const char *oldpath,const char *newpath);
int     unlink(const char *path);
mode_t  umask(mode_t mask);
int     isatty(int fd);
/* TTY identity is bounded to verified task-owned standard descriptors. */
char   *ttyname(int fd);
int     ttyname_r(int fd,char *buf,size_t size);
/* Bounded runtime constants: only _SC_PAGESIZE/_SC_PAGE_SIZE and _SC_CLK_TCK
 * are supported; other selectors fail with EINVAL. */
long    sysconf(int name);
size_t  confstr(int name,char *buf,size_t len);
int     getpagesize(void);
/* Read-only local nodename; `name` must hold it and its NUL terminator. */
int     gethostname(char *name,size_t len);

/* POSIX short-option parser. It supports grouped options, `--` and required
 * arguments (`:` in optstring); GNU long options and optional arguments are
 * intentionally absent. `opterr` is retained for source compatibility but
 * this freestanding runtime emits no diagnostics. */
extern char *optarg;
extern int optind,opterr,optopt;
int     getopt(int argc,char *const argv[],const char *optstring);
pid_t   getpid(void);
pid_t   getppid(void);
/* Bounded process-group/session identity: targets are self or direct children;
 * there is no controlling terminal or full job-control implementation. */
pid_t   getpgid(pid_t pid);
int     setpgid(pid_t pid,pid_t pgid);
pid_t   getpgrp(void);
pid_t   getsid(pid_t pid);
pid_t   setsid(void);
uid_t   getuid(void);
gid_t   getgid(void);
/* The supplementary group vector is immutable and empty. `getgroups` returns
 * 0 for any non-negative capacity; mutation helpers return EPERM. */
int     getgroups(int size,gid_t list[]);
int     setgroups(size_t size,const gid_t list[]);
int     initgroups(const char *user,gid_t group);
/* ATMKoala has one credential per task at present, so effective IDs equal
 * real IDs. setuid/setgid and supplementary groups are not implemented. */
uid_t   geteuid(void);
gid_t   getegid(void);
int     getresuid(uid_t *real,uid_t *effective,uid_t *saved);
int     getresgid(gid_t *real,gid_t *effective,gid_t *saved);
/* `kill(pid,0)` probes a positive self/direct-child PID without delivery.
 * SIGTERM and SIGKILL may terminate only a direct child. Group PIDs, self
 * delivery and handlers remain unsupported until a complete signal model exists. */
int     kill(pid_t pid,int sig);
/* Static ET_EXEC only; bounded execve copies up to 16 argv entries, 16 envp
 * entries and 2 KiB of strings. On success the old image is replaced and this
 * call returns only on failure. */
int     execve(const char *path,char *const argv[],char *const envp[]);
void    _exit(int status) __attribute__((noreturn));

#endif
