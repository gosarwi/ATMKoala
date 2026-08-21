#ifndef NATIVE_PIPE_H
#define NATIVE_PIPE_H

#include <stdint.h>
#include "sched.h"

/* Small, fixed-capacity anonymous-pipe foundation.  Endpoints are task-owned
 * descriptors; the object is shared only through dup/dup2 until fork-style
 * descriptor inheritance exists. */
#define ATM_PIPE_MAX  8
#define ATM_PIPE_CAP  1024

#define ATM_POLLIN   0x0001u
#define ATM_POLLOUT  0x0004u
#define ATM_POLLERR  0x0008u
#define ATM_POLLHUP  0x0010u
#define ATM_POLLNVAL 0x0020u

void    native_pipe_task_init(task_t *task);
void    native_pipe_task_cleanup(task_t *task);
int     native_pipe_create(task_t *task,int out_fds[2]);
int     native_pipe_is_fd(const task_t *task,int fd);
int     native_pipe_close(task_t *task,int fd);
int     native_pipe_dup(task_t *task,int oldfd,int newfd);
int     native_pipe_inherit(task_t *child,const task_t *parent,int fd);
int64_t native_pipe_read(task_t *task,int fd,void *buf,uint64_t count,int nonblock);
int64_t native_pipe_write(task_t *task,int fd,const void *buf,uint64_t count,int nonblock);
uint16_t native_pipe_poll(task_t *task,int fd,uint16_t events);
int     native_pipe_selftest(void);

#endif
