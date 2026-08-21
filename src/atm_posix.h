#ifndef ATM_POSIX_H
#define ATM_POSIX_H

/*
 * ATMKoala POSIX foundation.
 *
 * This is a kernel-side source-compatibility layer for the portable subset
 * used by native programs: descriptors, O_* flags, seek, stat and basic file
 * management.  It deliberately does not claim fork/exec, signals, user mode
 * or a hosted libc; those require the future process/ELF subsystem.
 */
#include "vfs.h"
#include <stdint.h>

/* VFS already exposes POSIX-shaped O_* and SEEK_* values. */
typedef vfs_stat_t atm_posix_stat_t;

#define ATM_POSIX_F_OK 0
#define ATM_POSIX_X_OK 1
#define ATM_POSIX_W_OK 2
#define ATM_POSIX_R_OK 4

typedef DIR_t atm_posix_dir_t;
typedef vfs_dirent_t atm_posix_dirent_t;
typedef struct { void *iov_base; uint64_t iov_len; } atm_posix_iovec_t;

int     atm_posix_open(const char *path, uint32_t flags, uint32_t mode);
int     atm_posix_creat(const char *path, uint32_t mode);
int     atm_posix_close(int fd);
int64_t atm_posix_read(int fd, void *buf, uint64_t count);
int64_t atm_posix_write(int fd, const void *buf, uint64_t count);
int64_t atm_posix_pread(int fd, void *buf, uint64_t count, uint64_t offset);
int64_t atm_posix_pwrite(int fd, const void *buf, uint64_t count, uint64_t offset);
int64_t atm_posix_readv(int fd, const atm_posix_iovec_t *iov, int iovcnt);
int64_t atm_posix_writev(int fd, const atm_posix_iovec_t *iov, int iovcnt);
int     atm_posix_fsync(int fd);
int     atm_posix_fdatasync(int fd);
int64_t atm_posix_lseek(int fd, int64_t offset, int whence);
int     atm_posix_stat(const char *path, atm_posix_stat_t *st);
int     atm_posix_lstat(const char *path, atm_posix_stat_t *st);
int     atm_posix_fstat(int fd, atm_posix_stat_t *st);
int     atm_posix_dup(int fd);
int     atm_posix_dup2(int fd, int newfd);
int     atm_posix_truncate(const char *path, uint64_t size);
int     atm_posix_ftruncate(int fd, uint64_t size);
int     atm_posix_chmod(const char *path, uint32_t mode);
int     atm_posix_chown(const char *path, uint32_t uid, uint32_t gid);
int     atm_posix_mkdir(const char *path, uint32_t mode);
int     atm_posix_rmdir(const char *path);
int     atm_posix_unlink(const char *path);
int     atm_posix_chdir(const char *path);
char   *atm_posix_getcwd(char *buf, size_t size);
int     atm_posix_access(const char *path, int mode);
uint32_t atm_posix_umask(uint32_t newmask);
int     atm_posix_isatty(int fd);
atm_posix_dir_t *atm_posix_opendir(const char *path);
atm_posix_dirent_t *atm_posix_readdir(atm_posix_dir_t *dir);
int     atm_posix_closedir(atm_posix_dir_t *dir);
int     atm_posix_rename(const char *oldpath, const char *newpath);
int     atm_posix_link(const char *oldpath, const char *newpath);
int     atm_posix_symlink(const char *target, const char *linkpath);
int     atm_posix_readlink(const char *path, char *buf, size_t size);
uint32_t atm_posix_getuid(void);
uint32_t atm_posix_getgid(void);
uint32_t atm_posix_getpid(void);
uint32_t atm_posix_getppid(void);

/* Feature bits reported by `posix status`. */
#define ATM_POSIX_FILES   0x01u
#define ATM_POSIX_UIDGID  0x02u
#define ATM_POSIX_PATHS   0x04u
#define ATM_POSIX_FD      0x08u
#define ATM_POSIX_META    0x10u
#define ATM_POSIX_LINKS   0x20u
#define ATM_POSIX_TRUNC   0x40u
#define ATM_POSIX_DIR     0x80u
#define ATM_POSIX_CWD     0x100u
#define ATM_POSIX_ACCESS  0x200u
#define ATM_POSIX_TTY     0x400u
#define ATM_POSIX_IOV     0x800u
#define ATM_POSIX_SYNC    0x1000u
#define ATM_POSIX_TASKCTX 0x2000u
#define ATM_POSIX_TIME    0x4000u
#define ATM_POSIX_SELECT  0x8000u
uint32_t atm_posix_features(void);
int      atm_posix_selftest(void);

#endif
