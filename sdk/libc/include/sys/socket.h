#ifndef ATM_LIBC_SYS_SOCKET_H
#define ATM_LIBC_SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define MSG_DONTWAIT 0x40u

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };

int socket(int domain,int type,int protocol);
int connect(int fd,const struct sockaddr *addr,socklen_t addrlen);
int bind(int fd,const struct sockaddr *addr,socklen_t addrlen);
int listen(int fd,int backlog);
int accept(int fd,struct sockaddr *addr,socklen_t *addrlen);
/* TCP stream only, 1..512 byte transfers. `flags` accepts zero on send and
 * zero or MSG_DONTWAIT on recv; all other message flags are rejected. */
ssize_t send(int fd,const void *buf,size_t len,int flags);
ssize_t recv(int fd,void *buf,size_t len,int flags);

#endif
