#ifndef ATM_LIBC_SYS_STAT_H
#define ATM_LIBC_SYS_STAT_H

#include <stdint.h>

typedef struct { uint64_t sec; uint32_t nsec; } atm_timespec_t;
typedef struct stat {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    atm_timespec_t st_atime;
    atm_timespec_t st_mtime;
    atm_timespec_t st_ctime;
    uint32_t inode;
    uint32_t size;
    uint32_t type;
    uint8_t perms;
    uint32_t uid, gid;
} stat_t;

#define S_IFMT  0xF000
#define S_IFREG 0x8000
#define S_IFDIR 0x4000
#define S_ISREG(m) (((m)&S_IFMT)==S_IFREG)
#define S_ISDIR(m) (((m)&S_IFMT)==S_IFDIR)

int stat(const char *path,stat_t *st);
int lstat(const char *path,stat_t *st);
int fstat(int fd,stat_t *st);
int fstatat(int dirfd,const char *path,stat_t *st,int flags);
int chmod(const char *path,mode_t mode);
#endif
