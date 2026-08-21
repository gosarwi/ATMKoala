#ifndef ATM_NATIVE_FD_H
#define ATM_NATIVE_FD_H

#include <stdint.h>
#include "sched.h"
#include "atm_posix.h"

#define ATM_NATIVE_O_ACCMODE  0x0003u
#define ATM_NATIVE_O_CLOEXEC  0x2000u
#define ATM_NATIVE_O_NONBLOCK 0x4000u
#define ATM_NATIVE_F_DUPFD    0
#define ATM_NATIVE_F_GETFD    1
#define ATM_NATIVE_F_SETFD    2
#define ATM_NATIVE_F_GETFL    3
#define ATM_NATIVE_F_SETFL    4
#define ATM_NATIVE_FD_CLOEXEC 1u
#define ATM_NATIVE_POLL_MAX   16

typedef struct { int fd; uint16_t events; uint16_t revents; } atm_native_pollfd_t;

/* Per-native-task descriptor namespace. The current VFS backend remains
 * shared, but user-visible integer handles are owned by a task and are
 * translated before a VFS operation. */
void    native_fd_task_init(task_t *task);
void    native_fd_task_cleanup(task_t *task);
/* Releases only descriptors carrying FD_CLOEXEC; used after a successful
 * replacement image has been fully prepared. */
void    native_fd_close_on_exec(task_t *task);
/* Duplicates VFS and pipe endpoints into child. Socket descriptors deliberately
 * reject inheritance until their multi-owner lifetime model is designed. */
int     native_fd_task_inherit(task_t *child,const task_t *parent);
int     native_fd_open(task_t *task, const char *path, uint32_t flags, uint32_t mode);
int     native_fd_pipe(task_t *task, int out_fds[2]);
int     native_fd_close(task_t *task, int fd);
int64_t native_fd_read(task_t *task, int fd, void *buf, uint64_t count);
int64_t native_fd_write(task_t *task, int fd, const void *buf, uint64_t count);
int64_t native_fd_readv(task_t *task, int fd, const atm_posix_iovec_t *iov, int iovcnt);
int64_t native_fd_writev(task_t *task, int fd, const atm_posix_iovec_t *iov, int iovcnt);
int64_t native_fd_lseek(task_t *task, int fd, int64_t offset, int whence);
int64_t native_fd_pread(task_t *task, int fd, void *buf, uint64_t count, uint64_t offset);
int64_t native_fd_pwrite(task_t *task, int fd, const void *buf, uint64_t count, uint64_t offset);
int     native_fd_fstat(task_t *task, int fd, atm_posix_stat_t *st);
/* Reads one VFS directory entry through an owned native descriptor. */
int     native_fd_readdir(task_t *task,int fd,atm_posix_dirent_t *out);
int     native_fd_dup(task_t *task, int fd);
int     native_fd_dup2(task_t *task, int fd, int newfd);
int     native_fd_ftruncate(task_t *task, int fd, uint64_t size);
int     native_fd_fsync(task_t *task, int fd, int data_only);
int     native_fd_isatty(task_t *task, int fd);
/* Bounded fcntl subset: F_DUPFD, F_GETFD/F_SETFD and F_GETFL/F_SETFL.
 * FD_CLOEXEC is a descriptor flag; only O_NONBLOCK is mutable among status
 * flags, while access mode is captured at descriptor creation. */
int     native_fd_fcntl(task_t *task,int fd,int cmd,uint32_t arg);
int     native_fd_poll(task_t *task,atm_native_pollfd_t *fds,uint32_t nfds);
int     native_fd_selftest(void);

#endif
