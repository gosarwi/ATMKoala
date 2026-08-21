#ifndef ATM_LIBC_FCNTL_H
#define ATM_LIBC_FCNTL_H

#include <stdint.h>

#define O_RDONLY 0x0000u
#define O_ACCMODE 0x0003u
#define O_WRONLY 0x0001u
#define O_RDWR   0x0002u
#define O_CREAT  0x0040u
#define O_TRUNC  0x0200u
#define O_APPEND 0x0400u
#define O_EXCL 0x0080u
#define O_NOFOLLOW 0x0800u
#define O_DIRECTORY 0x1000u
#define O_CLOEXEC 0x2000u
#define O_NONBLOCK 0x4000u

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

/* Native v1.11 at-style subset: only AT_FDCWD is implemented. */
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100u

int open(const char *path,int flags,...);
int openat(int dirfd,const char *path,int flags,...);
int fcntl(int fd,int cmd,...);

#endif
