#ifndef VFS_H
#define VFS_H
/*
 * vfs.h — POSIX Virtual Filesystem for atmkoala
 *
 * Provides:
 *   - inode-based VFS with full POSIX permission model (rwxrwxrwx + SUID/SGID)
 *   - Hard links (multiple dentries → one inode)
 *   - Symbolic links
 *   - Timestamps: atime / mtime / ctime
 *   - POSIX file descriptors (0=stdin 1=stdout 2=stderr, up to FD_MAX)
 *   - lseek / SEEK_SET / SEEK_CUR / SEEK_END
 *   - dup / dup2
 *   - rename / link / symlink / readlink
 *   - truncate / ftruncate
 *   - opendir / readdir / closedir
 *   - stat / fstat / lstat
 *   - chmod / chown
 *   - Mountable filesystem drivers (ramfs, devfs, procfs)
 *   - /dev:  null  zero  random  tty  kmsg
 *   - /proc: version  cmdline  meminfo  cpuinfo  uptime  mounts  self/
 *   - /sys:  kernel/  net/
 *
 * Backward-compatible with the old VFS API used across the codebase.
 */

#include <stdint.h>
#include <stddef.h>

/* ── File types (inode i_mode lower bits) ────────────────── */
#define S_IFMT    0xF000
#define S_IFREG   0x8000   /* regular file     */
#define S_IFDIR   0x4000   /* directory        */
#define S_IFLNK   0xA000   /* symbolic link    */
#define S_IFCHR   0x2000   /* character device */
#define S_IFBLK   0x6000   /* block device     */
#define S_IFIFO   0x1000   /* FIFO / pipe      */
#define S_IFSOCK  0xC000   /* socket           */

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

/* ── Permission bits ─────────────────────────────────────── */
#define S_ISUID   04000
#define S_ISGID   02000
#define S_ISVTX   01000
#define S_IRWXU   00700
#define S_IRUSR   00400
#define S_IWUSR   00200
#define S_IXUSR   00100
#define S_IRWXG   00070
#define S_IRGRP   00040
#define S_IWGRP   00020
#define S_IXGRP   00010
#define S_IRWXO   00007
#define S_IROTH   00004
#define S_IWOTH   00002
#define S_IXOTH   00001

/* Default modes */
#define MODE_FILE   (S_IFREG | 0644)
#define MODE_DIR    (S_IFDIR | 0755)
#define MODE_LINK   (S_IFLNK | 0777)
#define MODE_CDEV   (S_IFCHR | 0666)
#define MODE_BDEV   (S_IFBLK | 0660)

/* ── Backward-compat aliases ─────────────────────────────── */
#define VFS_FILE    S_IFREG
#define VFS_DIR     S_IFDIR
#define VFS_CHARDEV S_IFCHR
#define VFS_BLKDEV  S_IFBLK
#define VFS_PIPE    S_IFIFO
#define VFS_SYMLINK S_IFLNK
#define VFS_MOUNT   0x40    /* synthetic mount-point flag */
#define PERM_R      4
#define PERM_W      2
#define PERM_X      1

/* ── Open flags (O_* compatible) ─────────────────────────── */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NOFOLLOW  0x0800
#define O_DIRECTORY 0x1000
#define O_CLOEXEC   0x2000

/* ── lseek whence ────────────────────────────────────────── */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* ── Error codes ─────────────────────────────────────────── */
#define ENOENT      2
#define EACCES      13
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define EMFILE      24   /* too many open files */
#define ENOSPC      28
#define EROFS       30
#define ENOTEMPTY   39
#define ELOOP       40   /* symlink loop */
#define ENAMETOOLONG 36

extern int errno;

/* ── Limits ──────────────────────────────────────────────── */
#define VFS_NAME_MAX    255
#define VFS_PATH_MAX    1024
#define FD_MAX          64
#define VFS_INODE_MAX   512
#define VFS_SYMLINK_MAX 8    /* max symlink hops */
#define VFS_LINK_MAX    127

/* ── Timestamps ──────────────────────────────────────────── */
typedef struct {
    uint64_t sec;
    uint32_t nsec;
} vfs_timespec_t;

/* ── stat structure (POSIX compatible) ───────────────────── */
typedef struct {
    uint64_t       st_ino;
    uint32_t       st_mode;   /* S_IFMT | permissions */
    uint32_t       st_nlink;
    uint32_t       st_uid;
    uint32_t       st_gid;
    uint64_t       st_size;
    uint32_t       st_blksize;
    uint64_t       st_blocks;
    vfs_timespec_t st_atime;
    vfs_timespec_t st_mtime;
    vfs_timespec_t st_ctime;
    /* backward compat */
    uint32_t       inode;     /* = st_ino & 0xFFFFFFFF */
    uint32_t       size;      /* = st_size & 0xFFFFFFFF */
    uint32_t       type;      /* legacy VFS_FILE / VFS_DIR ... */
    uint8_t        perms;     /* legacy 3-bit rwx for owner */
    uint32_t       uid, gid;
} vfs_stat_t;

/* ── Directory entry ─────────────────────────────────────── */
typedef struct {
    uint64_t ino;
    uint8_t  d_type;          /* DT_REG / DT_DIR / ... */
    char     name[VFS_NAME_MAX + 1];
    /* backward compat */
    uint32_t inode;
    uint8_t  type;
} vfs_dirent_t;

/* DT_ types */
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

/* ── Directory handle ────────────────────────────────────── */
typedef struct {
    char     path[VFS_PATH_MAX];
    uint32_t index;
    int      valid;
} DIR_t;
#define DIR DIR_t

/* ── Filesystem driver ops ───────────────────────────────── */
struct vfs_inode;

typedef struct vfs_fsops {
    /* inode ops */
    int      (*lookup) (struct vfs_inode *dir, const char *name,
                        struct vfs_inode **out);
    int      (*create) (struct vfs_inode *dir, const char *name,
                        uint32_t mode, struct vfs_inode **out);
    int      (*mkdir)  (struct vfs_inode *dir, const char *name,
                        uint32_t mode, struct vfs_inode **out);
    int      (*unlink) (struct vfs_inode *dir, const char *name);
    int      (*rmdir)  (struct vfs_inode *dir, const char *name);
    int      (*rename) (struct vfs_inode *old_dir, const char *old_name,
                        struct vfs_inode *new_dir, const char *new_name);
    int      (*link)   (struct vfs_inode *dir, const char *name,
                        struct vfs_inode *target);
    int      (*symlink)(struct vfs_inode *dir, const char *name,
                        const char *target);
    int      (*readlink)(struct vfs_inode *inode, char *buf, size_t bufsz);
    int      (*readdir)(struct vfs_inode *dir, uint32_t index,
                        vfs_dirent_t *out);
    /* file I/O ops */
    int64_t  (*read)   (struct vfs_inode *inode, uint64_t off,
                        void *buf, uint64_t size);
    int64_t  (*write)  (struct vfs_inode *inode, uint64_t off,
                        const void *buf, uint64_t size);
    int      (*truncate)(struct vfs_inode *inode, uint64_t size);
    /* metadata */
    int      (*stat)   (struct vfs_inode *inode, vfs_stat_t *st);
    int      (*chmod)  (struct vfs_inode *inode, uint32_t mode);
    int      (*chown)  (struct vfs_inode *inode, uint32_t uid, uint32_t gid);
    /* lifecycle */
    void     (*destroy)(struct vfs_inode *inode);
    /* synthetic read (for /proc, /sys, /dev) */
    int64_t  (*synth_read)(struct vfs_inode *inode, uint64_t off,
                            void *buf, uint64_t size);
} vfs_fsops_t;

/* ── Inode ───────────────────────────────────────────────── */
typedef struct vfs_inode {
    uint64_t       ino;
    uint32_t       mode;      /* S_IFMT | permissions */
    uint32_t       uid, gid;
    uint32_t       nlink;
    uint64_t       size;
    vfs_timespec_t atime, mtime, ctime;
    vfs_fsops_t   *ops;
    void          *priv;      /* fs-private data */
    /* for mount points: pointer to mounted root inode */
    struct vfs_inode *mountpoint;
    int            refcount;
} vfs_inode_t;

/* ── File descriptor ─────────────────────────────────────── */
typedef struct {
    vfs_inode_t *inode;
    uint64_t     offset;
    uint32_t     flags;       /* O_* flags */
    int          used;
    int          cloexec;
    char         path[VFS_PATH_MAX]; /* for /proc/self/fd/ */
} vfs_fd_t;

/* ── Public API ──────────────────────────────────────────── */

/* Init */
void         vfs_init(void);
vfs_inode_t *vfs_root(void);

/* Mount */
int          vfs_mount(const char *path, vfs_inode_t *root);
int          vfs_unmount(const char *path);

/* Path resolution */
vfs_inode_t *vfs_lookup(const char *path);
vfs_inode_t *vfs_lookup_nofollow(const char *path);

/* POSIX file ops */
int          vfs_open   (const char *path, uint32_t flags, uint32_t mode);
void         vfs_close  (int fd);
int64_t      vfs_read   (int fd, void *buf, uint64_t size);
int64_t      vfs_write  (int fd, const void *buf, uint64_t size);
int64_t      vfs_lseek  (int fd, int64_t offset, int whence);
int          vfs_dup    (int fd);
int          vfs_dup2   (int fd, int newfd);
int          vfs_stat   (const char *path, vfs_stat_t *st);
int          vfs_fstat  (int fd, vfs_stat_t *st);
int          vfs_lstat  (const char *path, vfs_stat_t *st);
int          vfs_chmod  (const char *path, uint32_t mode);
int          vfs_chown  (const char *path, uint32_t uid, uint32_t gid);

/* Current shell-session credentials used by VFS permission checks. */
void         vfs_set_credentials(uint32_t uid, uint32_t gid);
uint32_t     vfs_current_uid(void);
uint32_t     vfs_current_gid(void);

int          vfs_truncate(const char *path, uint64_t size);
int          vfs_ftruncate(int fd, uint64_t size);

/* Directory ops */
int          vfs_mkdir  (const char *path, uint32_t mode);
int          vfs_rmdir  (const char *path);
int          vfs_unlink (const char *path);
int          vfs_rename (const char *oldpath, const char *newpath);
int          vfs_link   (const char *oldpath, const char *newpath);
int          vfs_symlink(const char *target, const char *linkpath);
int          vfs_readlink(const char *path, char *buf, size_t sz);
int          vfs_create (const char *path, uint32_t mode);

/* Directory iteration */
DIR_t       *vfs_opendir (const char *path);
vfs_dirent_t*vfs_readdir_next(DIR_t *dir);
void         vfs_closedir(DIR_t *dir);

/* Legacy compat */
int          vfs_listdir(const char *path, char names[][VFS_NAME_MAX+1],
                         int *count);
vfs_dirent_t*vfs_readdir(const char *path, uint32_t index);

/* Filesystem registration */
typedef vfs_inode_t *(*vfs_fs_constructor_t)(void);
void         vfs_register_fs(const char *name, vfs_fs_constructor_t ctor);

/* Ramfs constructor */
vfs_inode_t *ramfs_create_root(void);

/* Backward-compat node access (used by legacy callers) */
typedef vfs_inode_t vfs_node_t;

#endif /* VFS_H */
