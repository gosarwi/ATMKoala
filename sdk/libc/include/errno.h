#ifndef ATM_LIBC_ERRNO_H
#define ATM_LIBC_ERRNO_H

/* Single-threaded v0.9 errno storage. This is intentionally replaced by
 * TLS-backed storage only when the native pthread/TLS ABI exists. */
int *__errno_location(void);
#define errno (*__errno_location())

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EBUSY   16
#define EEXIST  17
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define EMFILE  24
#define ENOSPC  28
#define EROFS   30
#define EPIPE   32
#define ERANGE  34
#define ENOSYS  38
#define ENOTEMPTY 39

#endif
