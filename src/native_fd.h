#ifndef ATM_NATIVE_FD_H
#define ATM_NATIVE_FD_H

#include <stdint.h>
#include "sched.h"
#include "atm_posix.h"

/* Per-native-task descriptor namespace. The current VFS backend remains
 * shared, but user-visible integer handles are owned by a task and are
 * translated before a VFS operation. */
void    native_fd_task_init(task_t *task);
void    native_fd_task_cleanup(task_t *task);
int     native_fd_open(task_t *task, const char *path, uint32_t flags, uint32_t mode);
int     native_fd_close(task_t *task, int fd);
int64_t native_fd_read(task_t *task, int fd, void *buf, uint64_t count);
int64_t native_fd_write(task_t *task, int fd, const void *buf, uint64_t count);
int64_t native_fd_lseek(task_t *task, int fd, int64_t offset, int whence);
int     native_fd_fstat(task_t *task, int fd, atm_posix_stat_t *st);
int     native_fd_selftest(void);

#endif
