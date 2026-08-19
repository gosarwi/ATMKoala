#ifndef POSIX_H
#define POSIX_H
/* posix.h — atmkoala v0.5 POSIX compatibility layer */
#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

/* errno */
extern int errno;
#define EPERM 1  /* Operation not permitted */
#define ENOENT 2 /* No such file or directory */
#define EIO 5    /* I/O error */
#define EBADF 9  /* Bad file descriptor */
#define ENOMEM 12 /* Out of memory */
#define EACCES 13 /* Permission denied */
#define EEXIST 17 /* File exists */
#define ENOTDIR 20 /* Not a directory */
#define EISDIR 21  /* Is a directory */
#define EINVAL 22  /* Invalid argument */
#define EMFILE 24  /* Too many open files */
#define ENOSYS 38  /* Function not implemented */
#define ENOTEMPTY 39 /* Directory not empty */

const char *strerror(int e);

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Seek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* stat mode bits */
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_ISREG(m) (((m)&0170000)==0100000)
#define S_ISDIR(m) (((m)&0170000)==0040000)

typedef struct {
    uint32_t st_ino, st_mode, st_size, st_uid, st_gid;
} posix_stat_t;

/* Directory */
typedef struct {
    char          name[VFS_NAME_MAX];
    unsigned char d_type;
    uint32_t      d_ino;
    uint32_t      _pos;
    char          _path[128];
} posix_dirent_t;

typedef struct {
    char path[128]; uint32_t pos;
    posix_dirent_t ent;
} posix_DIR;

#define DT_REG 8
#define DT_DIR 4

/* access() modes */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

/* Signal stubs */
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGTERM 15
#define SIGKILL  9
#define SIGINT   2
#define SIGHUP   1

typedef int pid_t;
typedef int uid_t;

/* Time */
typedef uint32_t time_t;
typedef uint32_t clock_t;
#define CLOCKS_PER_SEC 100

/* API */
void posix_init(void);
const char *strerror(int e);
int  p_open(const char *path, int flags, int mode);
int  p_close(int fd);
int  p_read(int fd, void *buf, size_t n);
int  p_write(int fd, const void *buf, size_t n);
int  p_stat(const char *path, posix_stat_t *st);
int  p_access(const char *path, int mode);
int  p_unlink(const char *path);
int  p_rename(const char *old, const char *newp);
int  p_mkdir(const char *path, int mode);
posix_DIR *p_opendir(const char *path);
posix_dirent_t *p_readdir(posix_DIR *d);
void p_closedir(posix_DIR *d);
pid_t p_getpid(void);
uid_t p_getuid(void);
char *p_getenv(const char *name);
int   p_setenv(const char *name, const char *val, int overwrite);
time_t p_time(time_t *t);
clock_t p_clock(void);
int p_printf(const char *fmt, ...);
int p_puts(const char *s);
int p_putchar(int c);
#endif
