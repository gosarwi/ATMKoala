#ifndef ATM_LIBC_UNISTD_H
#define ATM_LIBC_UNISTD_H

#include <stddef.h>
#include <stdint.h>

/* The SDK headers are self-contained for the initial static runtime. */
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int fd,void *buf,size_t count);
ssize_t write(int fd,const void *buf,size_t count);
int     close(int fd);
off_t   lseek(int fd,off_t offset,int whence);
int     getpid(void);
int     getppid(void);
void    _exit(int status) __attribute__((noreturn));

#endif
