#ifndef ATM_LIBC_SYS_SELECT_H
#define ATM_LIBC_SYS_SELECT_H

#include <stdint.h>
#include <sys/time.h>

#define FD_SETSIZE 64

typedef struct { uint64_t bits; } fd_set;

#define FD_ZERO(set) ((set)->bits=0ULL)
#define FD_SET(fd,set) do { if((fd)>=0 && (fd)<FD_SETSIZE) (set)->bits|=(1ULL<<(fd)); } while(0)
#define FD_CLR(fd,set) do { if((fd)>=0 && (fd)<FD_SETSIZE) (set)->bits&=~(1ULL<<(fd)); } while(0)
#define FD_ISSET(fd,set) (((fd)>=0 && (fd)<FD_SETSIZE)?(((set)->bits&(1ULL<<(fd)))!=0):0)

int select(int nfds,fd_set *readfds,fd_set *writefds,fd_set *exceptfds,struct timeval *timeout);

#endif
