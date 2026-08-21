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

ssize_t read(int fd,void *buf,size_t count);
ssize_t write(int fd,const void *buf,size_t count);
ssize_t pread(int fd,void *buf,size_t count,off_t offset);
ssize_t pwrite(int fd,const void *buf,size_t count,off_t offset);
int     close(int fd);
int     dup(int fd);
int     dup2(int fd,int newfd);
int     pipe(int pipefd[2]);
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
int     access(const char *path,int mode);
int     chdir(const char *path);
char   *getcwd(char *buf,size_t size);
int     mkdir(const char *path,mode_t mode);
int     rmdir(const char *path);
int     link(const char *oldpath,const char *newpath);
int     unlink(const char *path);
mode_t  umask(mode_t mask);
int     isatty(int fd);
pid_t   getpid(void);
pid_t   getppid(void);
uid_t   getuid(void);
gid_t   getgid(void);
int     kill(pid_t pid,int sig);
/* Static ET_EXEC only; argv accepts NULL or one argv[0], envp accepts NULL or
 * an empty vector. On success the old image is replaced and this call returns
 * only on failure. */
int     execve(const char *path,char *const argv[],char *const envp[]);
void    _exit(int status) __attribute__((noreturn));

#endif
