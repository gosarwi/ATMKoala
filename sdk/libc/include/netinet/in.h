#ifndef ATM_LIBC_NETINET_IN_H
#define ATM_LIBC_NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

struct in_addr { uint8_t s_addr[4]; };
struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};

static inline uint16_t htons(uint16_t x){ return (uint16_t)((x>>8)|(x<<8)); }
static inline uint16_t ntohs(uint16_t x){ return htons(x); }

#endif
