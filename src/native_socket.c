#include "native_socket.h"
#include "net_tcp.h"
#include "net.h"
#include "util.h"
#include <stddef.h>

/* Single-core v0.9 registry. Every slot has one owning task and one TCP
 * connection. No descriptor sharing, dup, fork inheritance or listeners. */
typedef struct {
    task_t *owner;
    atm_tcp_conn_t conn;
    int used;
} socket_slot_t;
static socket_slot_t slots[ATM_SOCKET_MAX];

static int valid_fd(const task_t *t,int fd){
    return t && t->fd_table_ready && fd>=3 && fd<TASK_FD_MAX && t->socket_map[fd]>=0 && t->socket_map[fd]<ATM_SOCKET_MAX;
}
static socket_slot_t *slot_for(task_t *t,int fd){
    if(!valid_fd(t,fd)) return NULL;
    socket_slot_t *s=&slots[t->socket_map[fd]];
    return s->used && s->owner==t?s:NULL;
}
void native_socket_task_init(task_t *t){ if(t) for(int i=0;i<TASK_FD_MAX;i++)t->socket_map[i]=-1; }
void native_socket_task_cleanup(task_t *t){
    if(!t)return; for(int fd=3;fd<TASK_FD_MAX;fd++) if(t->socket_map[fd]>=0) (void)native_socket_close(t,fd);
}
int native_socket_is_fd(const task_t *t,int fd){ return valid_fd(t,fd) && slots[t->socket_map[fd]].used && slots[t->socket_map[fd]].owner==t; }
int native_socket_open(task_t *t,int domain,int type,int protocol){
    if(!t||!t->fd_table_ready||domain!=ATM_AF_INET||type!=ATM_SOCK_STREAM||protocol!=ATM_IPPROTO_TCP)return -1;
    int fd=-1,slot=-1; for(int i=3;i<TASK_FD_MAX;i++)if(t->fd_map[i]<0&&t->socket_map[i]<0){fd=i;break;}
    for(int i=0;i<ATM_SOCKET_MAX;i++)if(!slots[i].used){slot=i;break;}
    if(fd<0||slot<0)return -1;
    kmemset(&slots[slot],0,sizeof(slots[slot]));slots[slot].owner=t;slots[slot].used=1;t->socket_map[fd]=slot;return fd;
}
int native_socket_connect(task_t *t,int fd,const atm_sockaddr_in_t *a,uint32_t timeout){
    socket_slot_t *s=slot_for(t,fd); if(!s||!a||a->family!=ATM_AF_INET)return -1;
    uint16_t port=(uint16_t)((a->port>>8)|(a->port<<8)); if(!port)return -1;
    return atm_tcp_connect(&s->conn,a->addr,port,timeout);
}
int native_socket_bind(task_t *t,int fd,const atm_sockaddr_in_t *a){
    socket_slot_t *s=slot_for(t,fd);if(!s||!a||a->family!=ATM_AF_INET||s->conn.state!=ATM_TCP_CLOSED)return -1;
    for(int i=0;i<4;i++)if(a->addr[i]&&(a->addr[i]!=(uint8_t)net.ip[i]))return -1;
    uint16_t port=(uint16_t)((a->port>>8)|(a->port<<8));if(!port)return -1;s->conn.local_port=port;return 0;
}
int native_socket_listen(task_t *t,int fd,int backlog){
    socket_slot_t *s=slot_for(t,fd);if(!s||backlog<0||backlog>1)return -1;
    return atm_tcp_listen(&s->conn,s->conn.local_port);
}
int native_socket_accept(task_t *t,int fd,atm_sockaddr_in_t *peer,uint32_t timeout){
    socket_slot_t *listener=slot_for(t,fd);if(!listener||listener->conn.state!=ATM_TCP_LISTEN)return -1;
    int child_fd=-1,child_slot=-1;for(int i=3;i<TASK_FD_MAX;i++)if(t->fd_map[i]<0&&t->socket_map[i]<0){child_fd=i;break;}
    for(int i=0;i<ATM_SOCKET_MAX;i++)if(!slots[i].used){child_slot=i;break;}if(child_fd<0||child_slot<0)return -1;
    kmemset(&slots[child_slot],0,sizeof(slots[child_slot]));slots[child_slot].used=1;slots[child_slot].owner=t;t->socket_map[child_fd]=child_slot;
    if(atm_tcp_accept(&listener->conn,&slots[child_slot].conn,timeout)<0){t->socket_map[child_fd]=-1;kmemset(&slots[child_slot],0,sizeof(slots[child_slot]));return -1;}
    if(peer){kmemset(peer,0,sizeof(*peer));peer->family=ATM_AF_INET;peer->port=(uint16_t)((slots[child_slot].conn.remote_port>>8)|(slots[child_slot].conn.remote_port<<8));for(int i=0;i<4;i++)peer->addr[i]=slots[child_slot].conn.remote_ip[i];}
    return child_fd;
}
int64_t native_socket_send(task_t *t,int fd,const void *b,uint64_t n){
    socket_slot_t *s=slot_for(t,fd); if(!s||!b||n>512)return -1; return atm_tcp_send(&s->conn,b,(uint16_t)n);
}
int64_t native_socket_recv(task_t *t,int fd,void *b,uint64_t n,uint32_t timeout){
    socket_slot_t *s=slot_for(t,fd); if(!s||!b||!n||n>512)return -1; return atm_tcp_recv(&s->conn,b,(uint16_t)n,timeout);
}
int native_socket_close(task_t *t,int fd){
    socket_slot_t *s=slot_for(t,fd); if(!s)return -1; (void)atm_tcp_close(&s->conn);int idx=t->socket_map[fd];t->socket_map[fd]=-1;kmemset(&slots[idx],0,sizeof(slots[idx]));return 0;
}
int native_socket_selftest(void){
    task_t t;kmemset(&t,0,sizeof(t));
    /* A zeroed synthetic task does not carry task_create()'s -1 free-FD
     * invariant; initialize it explicitly before exercising socket_open(). */
    for(int i=0;i<TASK_FD_MAX;i++)t.fd_map[i]=-1;
    t.fd_table_ready=1;
    native_socket_task_init(&t);
    int fd=native_socket_open(&t,ATM_AF_INET,ATM_SOCK_STREAM,ATM_IPPROTO_TCP); if(fd!=3||!native_socket_is_fd(&t,fd)||native_socket_close(&t,fd)<0||native_socket_is_fd(&t,fd))return -1;return 0;
}
