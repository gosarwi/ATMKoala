#ifndef ATM_LIBC_FCNTL_H
#define ATM_LIBC_FCNTL_H

#include <stdint.h>

#define O_RDONLY 0x0000u
#define O_WRONLY 0x0001u
#define O_RDWR   0x0002u
#define O_CREAT  0x0040u
#define O_TRUNC  0x0200u

int open(const char *path,int flags,...);

#endif
