#ifndef ATM_LIBC_SYS_UIO_H
#define ATM_LIBC_SYS_UIO_H

#include <stddef.h>
#include <unistd.h>

struct iovec {
    void   *iov_base;
    size_t  iov_len;
};

ssize_t readv(int fd,const struct iovec *iov,int iovcnt);
ssize_t writev(int fd,const struct iovec *iov,int iovcnt);
/* Positional vector I/O supports at most 16 vectors and regular VFS-backed
 * descriptors; it does not change the descriptor’s current offset. */
ssize_t preadv(int fd,const struct iovec *iov,int iovcnt,off_t offset);
ssize_t pwritev(int fd,const struct iovec *iov,int iovcnt,off_t offset);

#endif
