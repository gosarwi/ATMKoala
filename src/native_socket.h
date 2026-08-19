#ifndef ATM_NATIVE_SOCKET_H
#define ATM_NATIVE_SOCKET_H

#include <stdint.h>
#include "sched.h"

#define ATM_AF_INET 2
#define ATM_SOCK_STREAM 1
#define ATM_IPPROTO_TCP 6
#define ATM_SOCKET_MAX 16

typedef struct __attribute__((packed)) {
    uint16_t family;
    uint16_t port; /* network byte order */
    uint8_t addr[4];
    uint8_t zero[8];
} atm_sockaddr_in_t;

void native_socket_task_init(task_t *task);
void native_socket_task_cleanup(task_t *task);
int native_socket_open(task_t *task,int domain,int type,int protocol);
int native_socket_connect(task_t *task,int fd,const atm_sockaddr_in_t *addr,uint32_t timeout_ticks);
int native_socket_bind(task_t *task,int fd,const atm_sockaddr_in_t *addr);
int native_socket_listen(task_t *task,int fd,int backlog);
int native_socket_accept(task_t *task,int fd,atm_sockaddr_in_t *peer,uint32_t timeout_ticks);
int64_t native_socket_send(task_t *task,int fd,const void *buf,uint64_t count);
int64_t native_socket_recv(task_t *task,int fd,void *buf,uint64_t count,uint32_t timeout_ticks);
int native_socket_close(task_t *task,int fd);
int native_socket_is_fd(const task_t *task,int fd);
int native_socket_selftest(void);

#endif
