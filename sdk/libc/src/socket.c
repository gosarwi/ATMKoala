#include <sys/socket.h>
#include "atm_native_abi.h"
#include "internal.h"

int socket(int domain,int type,int protocol){
    return (int)__atm_sysret(atm_socket(domain,type,protocol));
}
int connect(int fd,const struct sockaddr *addr,socklen_t len){
    return (int)__atm_sysret(atm_connect(fd,addr,len));
}
int bind(int fd,const struct sockaddr *addr,socklen_t len){
    return (int)__atm_sysret(atm_bind(fd,addr,len));
}
int listen(int fd,int backlog){ return (int)__atm_sysret(atm_listen(fd,backlog)); }
int accept(int fd,struct sockaddr *addr,socklen_t *len){
    socklen_t n=addr?sizeof(struct sockaddr):0;
    if(addr && len && *len<sizeof(struct sockaddr)) return (int)__atm_sysret(-ATM_EINVAL);
    int rc=(int)__atm_sysret(atm_accept(fd,addr,n));
    if(rc>=0 && addr && len) *len=sizeof(struct sockaddr);
    return rc;
}
ssize_t send(int fd,const void *buf,size_t len,int flags){return (ssize_t)__atm_sysret(atm_send(fd,buf,(uint64_t)len,(uint32_t)flags));}
ssize_t recv(int fd,void *buf,size_t len,int flags){return (ssize_t)__atm_sysret(atm_recv(fd,buf,(uint64_t)len,(uint32_t)flags));}
