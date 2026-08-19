/* posix.c — atmkoala v0.5 POSIX layer */
#include "posix.h"
#include "vfs.h"
#include "vga.h"
#include "util.h"
#include "pit.h"
#include "sched.h"
#include "kmalloc.h"
#include "ossdk.h"
#include <stdint.h>
#include <stddef.h>

int errno = 0;

const char *strerror(int e) {
    switch(e) {
    case 0:         return "OK";
    case EPERM:     return "Operation not permitted";
    case ENOENT:    return "No such file or directory";
    case EIO:       return "I/O error";
    case EBADF:     return "Bad file descriptor";
    case ENOMEM:    return "Out of memory";
    case EACCES:    return "Permission denied";
    case EEXIST:    return "File exists";
    case ENOTDIR:   return "Not a directory";
    case EISDIR:    return "Is a directory";
    case EINVAL:    return "Invalid argument";
    case EMFILE:    return "Too many open files";
    case ENOSYS:    return "Not implemented";
    case ENOTEMPTY: return "Directory not empty";
    default:        return "Unknown error";
    }
}

int p_open(const char *path, int flags, int mode) {
    (void)mode;
    int fd = vfs_open(path, (uint32_t)flags, 0);
    if (fd < 0) { errno = ENOENT; return -1; }
    return fd;
}
int p_close(int fd) {
    if (fd <= 2) return 0;
    vfs_close(fd); return 0;
}
int p_read(int fd, void *buf, size_t n) {
    if (fd == STDIN_FILENO) {
        extern void fish_readline(char *out, int maxlen);
        fish_readline((char*)buf, (int)n);
        return (int)kstrlen((char*)buf);
    }
    int r = vfs_read(fd, buf, (uint32_t)n);
    if (r < 0) { errno = EIO; return -1; }
    return r;
}
int p_write(int fd, const void *buf, size_t n) {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char *s = (const char*)buf;
        for (size_t i = 0; i < n; i++) terminal_putchar(s[i]);
        return (int)n;
    }
    int r = vfs_write(fd, buf, (uint32_t)n);
    if (r < 0) { errno = EIO; return -1; }
    return r;
}
int p_stat(const char *path, posix_stat_t *st) {
    vfs_stat_t vs;
    if (vfs_stat(path, &vs) < 0) { errno = ENOENT; return -1; }
    st->st_ino  = vs.inode;
    st->st_size = vs.size;
    st->st_uid  = vs.uid;
    st->st_gid  = vs.gid;
    st->st_mode = (vs.type & VFS_DIR) ? S_IFDIR : S_IFREG;
    return 0;
}
int p_access(const char *path, int mode) {
    (void)mode;
    vfs_stat_t st;
    return vfs_stat(path, &st) < 0 ? (errno=ENOENT,-1) : 0;
}
int p_unlink(const char *path) {
    return vfs_unlink(path) < 0 ? (errno=ENOENT,-1) : 0;
}
int p_rename(const char *old, const char *newp) {
    static uint8_t tmp[4096];
    int fdo = vfs_open(old, O_RDONLY, 0);
    if (fdo < 0) { errno=ENOENT; return -1; }
    int n = vfs_read(fdo, tmp, sizeof(tmp)-1);
    vfs_close(fdo);
    int fdn = vfs_open(newp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fdn < 0) { errno=EACCES; return -1; }
    if (n > 0) vfs_write(fdn, tmp, (uint32_t)n);
    vfs_close(fdn);
    vfs_unlink(old);
    return 0;
}
int p_mkdir(const char *path, int mode) {
    (void)mode;
    return vfs_mkdir(path, 0755) < 0 ? (errno=EEXIST,-1) : 0;
}

/* DIR pool */
static posix_DIR _pool[4];
static int _used[4];
posix_DIR *p_opendir(const char *path) {
    for (int i=0;i<4;i++) if (!_used[i]) {
        _used[i]=1;
        kstrncpy(_pool[i].path,path,127);
        _pool[i].pos=0;
        return &_pool[i];
    }
    errno=EMFILE; return NULL;
}
posix_dirent_t *p_readdir(posix_DIR *d) {
    if (!d) return NULL;
    vfs_dirent_t *ve = vfs_readdir(d->path, d->pos);
    if (!ve) return NULL;
    d->pos++;
    kstrncpy(d->ent.name,ve->name,VFS_NAME_MAX-1);
    d->ent.d_ino  = ve->inode;
    d->ent.d_type = (ve->type&VFS_DIR)?DT_DIR:DT_REG;
    return &d->ent;
}
void p_closedir(posix_DIR *d) {
    if (!d) return;
    for (int i=0;i<4;i++) if (&_pool[i]==d) { _used[i]=0; return; }
}

pid_t p_getpid(void) { return 1; }
uid_t p_getuid(void) {
    extern int sudo_mode;
    return sudo_mode ? 0 : 1000;
}
char *p_getenv(const char *name) { return (char*)sdk_env_get(name); }
int p_setenv(const char *name, const char *val, int overwrite) {
    if (!overwrite && p_getenv(name)) return 0;
    sdk_env_set(name,val); return 0;
}
time_t p_time(time_t *t) {
    time_t s=(time_t)(sched_uptime_ticks()/100);
    if (t) *t=s; return s;
}
clock_t p_clock(void) { return (clock_t)sched_uptime_ticks(); }
int p_printf(const char *fmt, ...) {
    char buf[256]; int n;
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    n = ksnprintf(buf,sizeof(buf),fmt,ap);
    __builtin_va_end(ap);
    terminal_write(buf); return n;
}
int p_puts(const char *s) { terminal_writeln(s); return 0; }
int p_putchar(int c) { terminal_putchar((char)c); return c; }

void posix_init(void) {
    errno=0;
    for (int i=0;i<4;i++) _used[i]=0;
    /* Standard Unix directories */
    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/usr", 0755);
    vfs_mkdir("/usr/bin", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/tmp", 0755);
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/home/user", 0755);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/var/log", 0755);
    vfs_mkdir("/proc", 0755);
    vfs_mkdir("/dev", 0755);
    /* POSIX environment */
    sdk_env_set("HOME",  "/home/user");
    sdk_env_set("SHELL", "/bin/atsh");
    sdk_env_set("PATH",  "/bin:/usr/bin:/syls/bin");
    sdk_env_set("TERM",  "caramel-vt100");
    sdk_env_set("LANG",  "en_US.UTF-8");
    sdk_env_set("USER",  "user");
}
