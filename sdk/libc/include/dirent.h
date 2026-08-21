#ifndef ATM_LIBC_DIRENT_H
#define ATM_LIBC_DIRENT_H

#include <stdint.h>

#define NAME_MAX 255
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

/* User-owned wrapper around an opaque kernel directory-stream handle. */
typedef struct __attribute__((packed)) dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[NAME_MAX+1];
} dirent_t;

typedef struct DIR {
    int handle;
    dirent_t entry;
} DIR;

DIR      *opendir(const char *path);
dirent_t *readdir(DIR *dir);
int       closedir(DIR *dir);

#endif
