/*
 * vfs.c — POSIX Virtual Filesystem for atmkoala
 *
 * Layers:
 *   VFS core   — path resolution, fd table, mount table, symlink following
 *   ramfs      — in-memory fs: inodes, dentries, hard/soft links, timestamps
 *   devfs      — /dev: null, zero, random, tty, kmsg
 *   procfs     — /proc: version, cmdline, meminfo, cpuinfo, uptime, mounts
 *   sysfs      — /sys: kernel/, net/
 */

#include "vfs.h"
#include "kmalloc.h"
#include "util.h"
#include "pit.h"
#include <stdint.h>
#include <stddef.h>

int errno = 0;

/* The kernel has no separate user-mode processes yet.  These credentials
 * describe the active console session and are intentionally centralised here
 * so every VFS entry point applies the same permission model. */
static uint32_t g_session_uid = 0;
static uint32_t g_session_gid = 0;

void vfs_set_credentials(uint32_t uid, uint32_t gid) { g_session_uid=uid; g_session_gid=gid; }
uint32_t vfs_current_uid(void) { return g_session_uid; }
uint32_t vfs_current_gid(void) { return g_session_gid; }

/* ════════════════════════════════════════════════════════════
 *  Helpers
 * ════════════════════════════════════════════════════════════ */

static vfs_timespec_t now(void) {
    vfs_timespec_t t = { pit_get_ticks() / 100, 0 };
    return t;
}

static void inode_touch_a(vfs_inode_t *i) { i->atime = now(); }
static void inode_touch_m(vfs_inode_t *i) { i->mtime = now(); i->ctime = now(); }

/* requested bits use the legacy PERM_R/PERM_W/PERM_X values (4/2/1). */
static int vfs_access(const vfs_inode_t *inode, uint32_t wanted) {
    if (!inode) return 0;
    if (g_session_uid == 0) return 1;
    uint32_t bits;
    if (g_session_uid == inode->uid) bits=(inode->mode >> 6) & 7U;
    else if (g_session_gid == inode->gid) bits=(inode->mode >> 3) & 7U;
    else bits=inode->mode & 7U;
    return (bits & wanted) == wanted;
}

static int vfs_require(const vfs_inode_t *inode, uint32_t wanted) {
    if (vfs_access(inode,wanted)) return 0;
    errno=EACCES;
    return -1;
}

static vfs_inode_t *inode_alloc(vfs_fsops_t *ops, uint32_t mode, void *priv) {
    vfs_inode_t *i = (vfs_inode_t *)kmalloc(sizeof(vfs_inode_t));
    if (!i) return NULL;
    static uint64_t next_ino = 1;
    kmemset(i, 0, sizeof(*i));
    i->ino      = next_ino++;
    i->mode     = mode;
    i->uid      = g_session_uid;
    i->gid      = g_session_gid;
    i->nlink    = 1;
    i->ops      = ops;
    i->priv     = priv;
    i->refcount = 1;
    i->atime = i->mtime = i->ctime = now();
    return i;
}

static void inode_ref(vfs_inode_t *i)   { if (i) i->refcount++; }
static void inode_unref(vfs_inode_t *i) {
    if (!i) return;
    if (--i->refcount <= 0) {
        if (i->ops && i->ops->destroy) i->ops->destroy(i);
        kfree(i);
    }
}

/* ════════════════════════════════════════════════════════════
 *  RAMFS
 * ════════════════════════════════════════════════════════════ */

/* Dentry: one name → one inode, chained list */
typedef struct ramfs_dentry {
    char                  name[VFS_NAME_MAX + 1];
    vfs_inode_t          *inode;
    struct ramfs_dentry  *next;
} ramfs_dentry_t;

/* Private data for directory inodes */
typedef struct {
    ramfs_dentry_t *children; /* linked list */
    uint32_t        count;
} ramfs_dir_t;

/* Private data for regular file inodes */
typedef struct {
    uint8_t  *data;
    uint64_t  size;
    uint64_t  capacity;
} ramfs_file_t;

/* Private data for symlink inodes */
typedef struct {
    char target[VFS_PATH_MAX];
} ramfs_link_t;

/* forward declare ramfs ops */
static vfs_fsops_t ramfs_ops;
static vfs_fsops_t ramfs_dir_ops;

/* ── dentry helpers ──────────────────────────────────────── */

static ramfs_dentry_t *dentry_find(ramfs_dir_t *d, const char *name) {
    for (ramfs_dentry_t *e = d->children; e; e = e->next)
        if (kstrcmp(e->name, name) == 0) return e;
    return NULL;
}

static int dentry_add(ramfs_dir_t *d, const char *name, vfs_inode_t *inode) {
    if (dentry_find(d, name)) return -EEXIST;
    ramfs_dentry_t *e = (ramfs_dentry_t *)kmalloc(sizeof(ramfs_dentry_t));
    if (!e) return -ENOSPC;
    kstrcpy(e->name, name);
    e->inode = inode;
    inode_ref(inode);
    e->next = d->children;
    d->children = e;
    d->count++;
    return 0;
}

static int dentry_remove(ramfs_dir_t *d, const char *name) {
    ramfs_dentry_t **pp = &d->children;
    while (*pp) {
        ramfs_dentry_t *e = *pp;
        if (kstrcmp(e->name, name) == 0) {
            *pp = e->next;
            inode_unref(e->inode);
            kfree(e);
            d->count--;
            return 0;
        }
        pp = &e->next;
    }
    return -ENOENT;
}

/* ── ramfs inode ops ─────────────────────────────────────── */

static int ramfs_lookup(vfs_inode_t *dir, const char *name,
                        vfs_inode_t **out) {
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    /* . and .. */
    if (kstrcmp(name, ".") == 0)  { *out = dir; inode_ref(dir); return 0; }
    if (kstrcmp(name, "..") == 0) { *out = dir; inode_ref(dir); return 0; }
    ramfs_dentry_t *e = dentry_find(d, name);
    if (!e) return -ENOENT;
    *out = e->inode;
    inode_ref(*out);
    return 0;
}

static int ramfs_create_impl(vfs_inode_t *dir, const char *name,
                              uint32_t mode, vfs_inode_t **out) {
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    if (dentry_find(d, name)) return -EEXIST;

    ramfs_file_t *fp = (ramfs_file_t *)kmalloc(sizeof(ramfs_file_t));
    if (!fp) return -ENOSPC;
    kmemset(fp, 0, sizeof(*fp));

    vfs_inode_t *ni = inode_alloc(&ramfs_ops, mode, fp);
    if (!ni) { kfree(fp); return -ENOSPC; }

    int r = dentry_add(d, name, ni);
    if (r < 0) { inode_unref(ni); return r; }

    inode_touch_m(dir);
    if (out) { *out = ni; inode_ref(ni); }
    inode_unref(ni);
    return 0;
}

static int ramfs_mkdir_impl(vfs_inode_t *dir, const char *name,
                             uint32_t mode, vfs_inode_t **out) {
    ramfs_dir_t *pd = (ramfs_dir_t *)dir->priv;
    if (dentry_find(pd, name)) return -EEXIST;

    ramfs_dir_t *nd = (ramfs_dir_t *)kmalloc(sizeof(ramfs_dir_t));
    if (!nd) return -ENOSPC;
    nd->children = NULL;
    nd->count = 0;

    vfs_inode_t *ni = inode_alloc(&ramfs_dir_ops, mode | S_IFDIR, nd);
    if (!ni) { kfree(nd); return -ENOSPC; }
    ni->nlink = 2; /* . and entry in parent */

    /* Add . */
    dentry_add(nd, ".", ni);
    /* Add .. (points to parent — same inode for now, simplified) */
    dentry_add(nd, "..", dir);

    int r = dentry_add(pd, name, ni);
    if (r < 0) { inode_unref(ni); return r; }

    dir->nlink++; /* one more subdir */
    inode_touch_m(dir);
    if (out) { *out = ni; inode_ref(ni); }
    inode_unref(ni);
    return 0;
}

static int ramfs_unlink_impl(vfs_inode_t *dir, const char *name) {
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    ramfs_dentry_t *e = dentry_find(d, name);
    if (!e) return -ENOENT;
    if (S_ISDIR(e->inode->mode)) return -EISDIR;
    e->inode->nlink--;
    int r = dentry_remove(d, name);
    inode_touch_m(dir);
    return r;
}

static int ramfs_rmdir_impl(vfs_inode_t *dir, const char *name) {
    ramfs_dir_t *pd = (ramfs_dir_t *)dir->priv;
    ramfs_dentry_t *e = dentry_find(pd, name);
    if (!e) return -ENOENT;
    if (!S_ISDIR(e->inode->mode)) return -ENOTDIR;
    ramfs_dir_t *cd = (ramfs_dir_t *)e->inode->priv;
    /* Only . and .. allowed */
    if (cd->count > 2) return -ENOTEMPTY;
    e->inode->nlink--;
    dir->nlink--;
    int r = dentry_remove(pd, name);
    inode_touch_m(dir);
    return r;
}

static int ramfs_link_impl(vfs_inode_t *dir, const char *name,
                            vfs_inode_t *target) {
    if (S_ISDIR(target->mode)) return -EISDIR;
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    int r = dentry_add(d, name, target);
    if (r == 0) { target->nlink++; inode_touch_m(dir); }
    return r;
}

static int ramfs_symlink_impl(vfs_inode_t *dir, const char *name,
                               const char *target) {
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    if (dentry_find(d, name)) return -EEXIST;

    ramfs_link_t *lp = (ramfs_link_t *)kmalloc(sizeof(ramfs_link_t));
    if (!lp) return -ENOSPC;
    kstrcpy(lp->target, target);

    vfs_inode_t *ni = inode_alloc(&ramfs_ops, MODE_LINK, lp);
    if (!ni) { kfree(lp); return -ENOSPC; }
    ni->size = (uint64_t)kstrlen(target);

    int r = dentry_add(d, name, ni);
    if (r < 0) { inode_unref(ni); return r; }
    inode_touch_m(dir);
    inode_unref(ni);
    return 0;
}

static int ramfs_readlink_impl(vfs_inode_t *inode, char *buf, size_t sz) {
    if (!S_ISLNK(inode->mode)) return -EINVAL;
    ramfs_link_t *lp = (ramfs_link_t *)inode->priv;
    size_t n = kstrlen(lp->target);
    if (n >= sz) n = sz - 1;
    for (size_t i = 0; i < n; i++) buf[i] = lp->target[i];
    buf[n] = 0;
    return (int)n;
}

static int ramfs_readdir_impl(vfs_inode_t *dir, uint32_t index,
                               vfs_dirent_t *out) {
    ramfs_dir_t *d = (ramfs_dir_t *)dir->priv;
    uint32_t i = 0;
    for (ramfs_dentry_t *e = d->children; e; e = e->next, i++) {
        if (i == index) {
            kstrcpy(out->name, e->name);
            out->ino    = e->inode->ino;
            out->inode  = (uint32_t)e->inode->ino;
            out->d_type = S_ISDIR(e->inode->mode) ? DT_DIR :
                          S_ISLNK(e->inode->mode) ? DT_LNK :
                          S_ISCHR(e->inode->mode) ? DT_CHR : DT_REG;
            out->type   = (uint8_t)e->inode->mode; /* legacy */
            return 0;
        }
    }
    return -1; /* end of dir */
}

static int ramfs_rename_impl(vfs_inode_t *old_dir, const char *old_name,
                              vfs_inode_t *new_dir, const char *new_name) {
    ramfs_dir_t *od = (ramfs_dir_t *)old_dir->priv;
    ramfs_dir_t *nd = (ramfs_dir_t *)new_dir->priv;

    ramfs_dentry_t *e = dentry_find(od, old_name);
    if (!e) return -ENOENT;

    vfs_inode_t *target = e->inode;
    inode_ref(target);

    /* Remove old entry */
    dentry_remove(od, old_name);

    /* Remove new entry if it exists */
    if (dentry_find(nd, new_name))
        dentry_remove(nd, new_name);

    /* Add new entry */
    int r = dentry_add(nd, new_name, target);
    inode_unref(target);

    inode_touch_m(old_dir);
    inode_touch_m(new_dir);
    return r;
}

static int64_t ramfs_read_impl(vfs_inode_t *inode, uint64_t off,
                                void *buf, uint64_t size) {
    if (S_ISLNK(inode->mode)) {
        /* reading a symlink */
        ramfs_link_t *lp = (ramfs_link_t *)inode->priv;
        uint64_t tlen = (uint64_t)kstrlen(lp->target);
        if (off >= tlen) return 0;
        uint64_t avail = tlen - off;
        if (size > avail) size = avail;
        for (uint64_t i = 0; i < size; i++)
            ((uint8_t *)buf)[i] = (uint8_t)lp->target[off + i];
        return (int64_t)size;
    }
    ramfs_file_t *fp = (ramfs_file_t *)inode->priv;
    if (!fp || off >= fp->size) return 0;
    uint64_t avail = fp->size - off;
    if (size > avail) size = avail;
    for (uint64_t i = 0; i < size; i++)
        ((uint8_t *)buf)[i] = fp->data[off + i];
    inode_touch_a(inode);
    return (int64_t)size;
}

static int64_t ramfs_write_impl(vfs_inode_t *inode, uint64_t off,
                                  const void *buf, uint64_t size) {
    ramfs_file_t *fp = (ramfs_file_t *)inode->priv;
    if (!fp) return -EINVAL;

    uint64_t need = off + size;
    if (need > fp->capacity) {
        uint64_t newcap = need + 512;
        uint8_t *newbuf = (uint8_t *)kmalloc((uint32_t)newcap);
        if (!newbuf) return -ENOSPC;
        kmemset(newbuf, 0, (uint32_t)newcap);
        if (fp->data) {
            for (uint64_t i = 0; i < fp->size; i++) newbuf[i] = fp->data[i];
            kfree(fp->data);
        }
        fp->data     = newbuf;
        fp->capacity = newcap;
    }
    for (uint64_t i = 0; i < size; i++)
        fp->data[off + i] = ((const uint8_t *)buf)[i];
    if (off + size > fp->size) fp->size = off + size;
    inode->size = fp->size;
    inode_touch_m(inode);
    return (int64_t)size;
}

static int ramfs_truncate_impl(vfs_inode_t *inode, uint64_t size) {
    ramfs_file_t *fp = (ramfs_file_t *)inode->priv;
    if (!fp) return -EINVAL;
    if (size < fp->size) {
        fp->size = size;
    } else if (size > fp->capacity) {
        uint8_t *nb = (uint8_t *)kmalloc((uint32_t)size);
        if (!nb) return -ENOSPC;
        kmemset(nb, 0, (uint32_t)size);
        if (fp->data) {
            for (uint64_t i = 0; i < fp->size; i++) nb[i] = fp->data[i];
            kfree(fp->data);
        }
        fp->data     = nb;
        fp->capacity = size;
        fp->size     = size;
    } else {
        /* zero-extend */
        for (uint64_t i = fp->size; i < size; i++) fp->data[i] = 0;
        fp->size = size;
    }
    inode->size = fp->size;
    inode_touch_m(inode);
    return 0;
}

static int ramfs_stat_impl(vfs_inode_t *inode, vfs_stat_t *st) {
    kmemset(st, 0, sizeof(*st));
    st->st_ino    = inode->ino;
    st->st_mode   = inode->mode;
    st->st_nlink  = inode->nlink;
    st->st_uid    = inode->uid;
    st->st_gid    = inode->gid;
    st->st_size   = inode->size;
    st->st_blksize= 512;
    st->st_blocks = (inode->size + 511) / 512;
    st->st_atime  = inode->atime;
    st->st_mtime  = inode->mtime;
    st->st_ctime  = inode->ctime;
    /* backward compat */
    st->inode  = (uint32_t)inode->ino;
    st->size   = (uint32_t)inode->size;
    st->uid    = inode->uid;
    st->gid    = inode->gid;
    st->perms  = (uint8_t)((inode->mode >> 6) & 7);
    if      (S_ISREG(inode->mode))  st->type = VFS_FILE;
    else if (S_ISDIR(inode->mode))  st->type = VFS_DIR;
    else if (S_ISLNK(inode->mode))  st->type = VFS_SYMLINK;
    else if (S_ISCHR(inode->mode))  st->type = VFS_CHARDEV;
    else                            st->type = VFS_FILE;
    return 0;
}

static int ramfs_chmod_impl(vfs_inode_t *inode, uint32_t mode) {
    inode->mode = (inode->mode & S_IFMT) | (mode & ~S_IFMT);
    inode->ctime = now();
    return 0;
}

static int ramfs_chown_impl(vfs_inode_t *inode, uint32_t uid, uint32_t gid) {
    inode->uid = uid; inode->gid = gid;
    inode->ctime = now();
    return 0;
}

static void ramfs_destroy_file(vfs_inode_t *inode) {
    if (!inode->priv) return;
    if (S_ISREG(inode->mode)) {
        ramfs_file_t *fp = (ramfs_file_t *)inode->priv;
        if (fp->data) kfree(fp->data);
        kfree(fp);
    } else if (S_ISLNK(inode->mode)) {
        kfree(inode->priv);
    } else if (S_ISDIR(inode->mode)) {
        ramfs_dir_t *d = (ramfs_dir_t *)inode->priv;
        /* free all dentries */
        ramfs_dentry_t *e = d->children;
        while (e) {
            ramfs_dentry_t *nx = e->next;
            /* inode_unref already called by dentry_remove earlier */
            kfree(e);
            e = nx;
        }
        kfree(d);
    }
}

/* Two ops tables: one for files/symlinks, one for dirs */
static vfs_fsops_t ramfs_ops = {
    .read      = ramfs_read_impl,
    .write     = ramfs_write_impl,
    .truncate  = ramfs_truncate_impl,
    .stat      = ramfs_stat_impl,
    .chmod     = ramfs_chmod_impl,
    .chown     = ramfs_chown_impl,
    .readlink  = ramfs_readlink_impl,
    .destroy   = ramfs_destroy_file,
};

static vfs_fsops_t ramfs_dir_ops = {
    .lookup    = ramfs_lookup,
    .create    = ramfs_create_impl,
    .mkdir     = ramfs_mkdir_impl,
    .unlink    = ramfs_unlink_impl,
    .rmdir     = ramfs_rmdir_impl,
    .rename    = ramfs_rename_impl,
    .link      = ramfs_link_impl,
    .symlink   = ramfs_symlink_impl,
    .readlink  = ramfs_readlink_impl,
    .readdir   = ramfs_readdir_impl,
    .stat      = ramfs_stat_impl,
    .chmod     = ramfs_chmod_impl,
    .chown     = ramfs_chown_impl,
    .destroy   = ramfs_destroy_file,
};

/* ── ramfs_create_root ────────────────────────────────────── */
vfs_inode_t *ramfs_create_root(void) {
    ramfs_dir_t *d = (ramfs_dir_t *)kmalloc(sizeof(ramfs_dir_t));
    if (!d) return NULL;
    d->children = NULL;
    d->count    = 0;

    vfs_inode_t *root = inode_alloc(&ramfs_dir_ops, MODE_DIR, d);
    if (!root) { kfree(d); return NULL; }
    root->nlink = 2;
    /* self-referencing . and .. for root */
    dentry_add(d, ".",  root);
    dentry_add(d, "..", root);
    return root;
}

/* ── Helper: mkdir -p inside ramfs from VFS api ─────────────
 * Creates a dir node as child of an existing parent inode.     */
static vfs_inode_t *ramfs_mkdir_child(vfs_inode_t *parent,
                                       const char *name, uint32_t mode) {
    vfs_inode_t *out = NULL;
    parent->ops->mkdir(parent, name, mode, &out);
    return out;
}

/* ── Helper: write a file's content directly into a parent ── */
static void ramfs_mkfile(vfs_inode_t *parent, const char *name,
                          const char *content) {
    vfs_inode_t *f = NULL;
    if (parent->ops->create(parent, name, MODE_FILE, &f) != 0) {
        /* already exists — look it up */
        parent->ops->lookup(parent, name, &f);
    }
    if (f && content) {
        uint64_t len = (uint64_t)kstrlen(content);
        f->ops->write(f, 0, content, len);
        inode_unref(f);
    }
}

/* ════════════════════════════════════════════════════════════
 *  DEVFS  (/dev)
 * ════════════════════════════════════════════════════════════ */

/* Synthetic read for /dev/zero */
static int64_t devzero_read(vfs_inode_t *i, uint64_t off,
                              void *buf, uint64_t size) {
    (void)i; (void)off;
    kmemset(buf, 0, (uint32_t)size);
    return (int64_t)size;
}
/* Synthetic read for /dev/null */
static int64_t devnull_read(vfs_inode_t *i, uint64_t off,
                              void *buf, uint64_t size) {
    (void)i; (void)off; (void)buf; (void)size;
    return 0;
}
/* Synthetic write for /dev/null */
static int64_t devnull_write(vfs_inode_t *i, uint64_t off,
                               const void *buf, uint64_t size) {
    (void)i; (void)off; (void)buf;
    return (int64_t)size;
}
/* Simple LCG random for /dev/random */
static uint32_t _rng = 0xDEADBEEF;
static int64_t devrandom_read(vfs_inode_t *i, uint64_t off,
                                void *buf, uint64_t size) {
    (void)i; (void)off;
    for (uint64_t n = 0; n < size; n++) {
        _rng = _rng * 1664525u + 1013904223u;
        ((uint8_t *)buf)[n] = (uint8_t)(_rng >> 16);
    }
    return (int64_t)size;
}

static vfs_fsops_t dev_null_ops   = { .read = devnull_read,
                                       .write= devnull_write,
                                       .stat = ramfs_stat_impl };
static vfs_fsops_t dev_zero_ops   = { .read = devzero_read,
                                       .write= devnull_write,
                                       .stat = ramfs_stat_impl };
static vfs_fsops_t dev_random_ops = { .read = devrandom_read,
                                       .stat = ramfs_stat_impl };

typedef struct {
    const char   *name;
    vfs_fsops_t  *ops;
    uint32_t      mode;
} dev_entry_t;

static dev_entry_t dev_entries[] = {
    { "null",     &dev_null_ops,   S_IFCHR | 0666 },
    { "zero",     &dev_zero_ops,   S_IFCHR | 0666 },
    { "random",   &dev_random_ops, S_IFCHR | 0444 },
    { "urandom",  &dev_random_ops, S_IFCHR | 0444 },
    { "tty",      &dev_null_ops,   S_IFCHR | 0620 },
    { "tty0",     &dev_null_ops,   S_IFCHR | 0620 },
    { "kmsg",     &dev_null_ops,   S_IFCHR | 0644 },
    { "stdin",    &dev_null_ops,   S_IFCHR | 0444 },
    { "stdout",   &dev_null_ops,   S_IFCHR | 0222 },
    { "stderr",   &dev_null_ops,   S_IFCHR | 0222 },
    { NULL, NULL, 0 }
};

static vfs_inode_t *devfs_create(void) {
    vfs_inode_t *root = ramfs_create_root();
    if (!root) return NULL;
    for (int i = 0; dev_entries[i].name; i++) {
        vfs_inode_t *di = inode_alloc(dev_entries[i].ops,
                                       dev_entries[i].mode, NULL);
        if (!di) continue;
        ramfs_link_impl(root, dev_entries[i].name, di);
        inode_unref(di);
    }
    /* /dev/fd → symlink to /proc/self/fd */
    ramfs_symlink_impl(root, "fd", "/proc/self/fd");
    /* disk devices (block, no actual driver) */
    vfs_inode_t *hda = inode_alloc(&dev_null_ops, S_IFBLK | 0660, NULL);
    if (hda) { ramfs_link_impl(root, "hda", hda); inode_unref(hda); }
    vfs_inode_t *sda = inode_alloc(&dev_null_ops, S_IFBLK | 0660, NULL);
    if (sda) { ramfs_link_impl(root, "sda", sda); inode_unref(sda); }
    return root;
}

/* ════════════════════════════════════════════════════════════
 *  PROCFS  (/proc)
 * ════════════════════════════════════════════════════════════ */

static int64_t proc_version_read(vfs_inode_t *i, uint64_t off,
                                   void *buf, uint64_t size) {
    (void)i;
    const char *s = "atmkoala 0.5.0 x86-64 (gcc 13.3) #1 SMP\n";
    uint64_t len = (uint64_t)kstrlen(s);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = s[off+k];
    return (int64_t)n;
}

static int64_t proc_cmdline_read(vfs_inode_t *i, uint64_t off,
                                   void *buf, uint64_t size) {
    (void)i;
    const char *s = "atmkoala quiet\n";
    uint64_t len = (uint64_t)kstrlen(s);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = s[off+k];
    return (int64_t)n;
}

static int64_t proc_uptime_read(vfs_inode_t *i, uint64_t off,
                                  void *buf, uint64_t size) {
    (void)i;
    char tmp[32];
    uint32_t secs = pit_get_ticks() / 100;
    ksnprintf(tmp, sizeof(tmp), "%u.00 %u.00\n", secs, secs);
    uint64_t len = (uint64_t)kstrlen(tmp);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = tmp[off+k];
    return (int64_t)n;
}

static int64_t proc_meminfo_read(vfs_inode_t *i, uint64_t off,
                                   void *buf, uint64_t size) {
    (void)i;
    char tmp[256];
    extern uint32_t heap_free_bytes(void);
    extern uint32_t heap_used_bytes(void);
    uint32_t total = 256 * 1024;
    uint32_t free  = heap_free_bytes() / 1024;
    uint32_t used  = heap_used_bytes() / 1024;
    ksnprintf(tmp, sizeof(tmp),
        "MemTotal:     %6u kB\n"
        "MemFree:      %6u kB\n"
        "MemAvailable: %6u kB\n"
        "Cached:            0 kB\n"
        "SwapTotal:         0 kB\n"
        "SwapFree:          0 kB\n",
        total, free, total - used);
    uint64_t len = (uint64_t)kstrlen(tmp);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = tmp[off+k];
    return (int64_t)n;
}

static int64_t proc_cpuinfo_read(vfs_inode_t *i, uint64_t off,
                                   void *buf, uint64_t size) {
    (void)i;
    const char *s =
        "processor\t: 0\n"
        "vendor_id\t: atmkoala\n"
        "cpu family\t: 6\n"
        "model name\t: atmkoala Virtual CPU x86-64\n"
        "cpu MHz\t\t: 100.000\n"
        "cache size\t: 256 KB\n"
        "flags\t\t: fpu pae lm pse nx\n";
    uint64_t len = (uint64_t)kstrlen(s);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = s[off+k];
    return (int64_t)n;
}

static int64_t proc_mounts_read(vfs_inode_t *i, uint64_t off,
                                  void *buf, uint64_t size) {
    (void)i;
    const char *s =
        "ramfs  /       ramfs  rw,nosuid,nodev   0 0\n"
        "proc   /proc   proc   ro,nosuid,nodev   0 0\n"
        "devfs  /dev    devfs  rw,nosuid         0 0\n"
        "sysfs  /sys    sysfs  ro,nosuid,nodev   0 0\n"
        "tmpfs  /tmp    tmpfs  rw,nosuid,nodev   0 0\n";
    uint64_t len = (uint64_t)kstrlen(s);
    if (off >= len) return 0;
    uint64_t n = len - off; if (n > size) n = size;
    for (uint64_t k = 0; k < n; k++) ((char *)buf)[k] = s[off+k];
    return (int64_t)n;
}

static vfs_fsops_t proc_synth_ops_ver  = { .read=proc_version_read,  .stat=ramfs_stat_impl };
static vfs_fsops_t proc_synth_ops_cmd  = { .read=proc_cmdline_read,  .stat=ramfs_stat_impl };
static vfs_fsops_t proc_synth_ops_upt  = { .read=proc_uptime_read,   .stat=ramfs_stat_impl };
static vfs_fsops_t proc_synth_ops_mem  = { .read=proc_meminfo_read,  .stat=ramfs_stat_impl };
static vfs_fsops_t proc_synth_ops_cpu  = { .read=proc_cpuinfo_read,  .stat=ramfs_stat_impl };
static vfs_fsops_t proc_synth_ops_mnt  = { .read=proc_mounts_read,   .stat=ramfs_stat_impl };

typedef struct { const char *name; vfs_fsops_t *ops; } proc_synth_t;
static proc_synth_t proc_files[] = {
    { "version",    &proc_synth_ops_ver },
    { "cmdline",    &proc_synth_ops_cmd },
    { "uptime",     &proc_synth_ops_upt },
    { "meminfo",    &proc_synth_ops_mem },
    { "cpuinfo",    &proc_synth_ops_cpu },
    { "mounts",     &proc_synth_ops_mnt },
    { "mtab",       &proc_synth_ops_mnt },
    { NULL, NULL }
};

static vfs_inode_t *procfs_create(void) {
    vfs_inode_t *root = ramfs_create_root();
    if (!root) return NULL;
    for (int i = 0; proc_files[i].name; i++) {
        vfs_inode_t *f = inode_alloc(proc_files[i].ops, S_IFREG | 0444, NULL);
        if (f) { ramfs_link_impl(root, proc_files[i].name, f); inode_unref(f); }
    }
    /* /proc/self → symlink to /proc/1 */
    ramfs_symlink_impl(root, "self", "/proc/1");
    /* /proc/1 → minimal process dir */
    vfs_inode_t *p1 = NULL;
    ramfs_mkdir_impl(root, "1", MODE_DIR, &p1);
    if (p1) {
        ramfs_mkfile(p1, "cmdline", "kernel\0");
        ramfs_mkfile(p1, "status",
            "Name:\tkernel\nState:\tR (running)\nPid:\t1\nPPid:\t0\n"
            "Uid:\t0 0 0 0\nGid:\t0 0 0 0\n");
        /* /proc/1/fd/ directory */
        vfs_inode_t *fd_dir = NULL;
        ramfs_mkdir_impl(p1, "fd", MODE_DIR, &fd_dir);
        if (fd_dir) {
            ramfs_symlink_impl(fd_dir, "0", "/dev/stdin");
            ramfs_symlink_impl(fd_dir, "1", "/dev/stdout");
            ramfs_symlink_impl(fd_dir, "2", "/dev/stderr");
            inode_unref(fd_dir);
        }
        inode_unref(p1);
    }
    return root;
}

/* ════════════════════════════════════════════════════════════
 *  SYSFS  (/sys) — read-only synthetic
 * ════════════════════════════════════════════════════════════ */

static vfs_inode_t *sysfs_create(void) {
    vfs_inode_t *root = ramfs_create_root();
    if (!root) return NULL;

    /* /sys/kernel/ */
    vfs_inode_t *kd = ramfs_mkdir_child(root, "kernel", MODE_DIR);
    if (kd) {
        ramfs_mkfile(kd, "hostname",   "atmkoala\n");
        ramfs_mkfile(kd, "osrelease",  "1.0.0\n");
        ramfs_mkfile(kd, "version",    "#1 SMP x86-64\n");
        ramfs_mkfile(kd, "arch",       "x86_64\n");
        ramfs_mkfile(kd, "pid_max",    "32768\n");
        inode_unref(kd);
    }

    /* /sys/net/ */
    vfs_inode_t *nd = ramfs_mkdir_child(root, "net", MODE_DIR);
    if (nd) {
        vfs_inode_t *lo = ramfs_mkdir_child(nd, "lo", MODE_DIR);
        if (lo) {
            ramfs_mkfile(lo, "address",   "00:00:00:00:00:00\n");
            ramfs_mkfile(lo, "operstate", "up\n");
            ramfs_mkfile(lo, "mtu",       "65536\n");
            inode_unref(lo);
        }
        vfs_inode_t *eth0 = ramfs_mkdir_child(nd, "eth0", MODE_DIR);
        if (eth0) {
            ramfs_mkfile(eth0, "operstate", "down\n");
            ramfs_mkfile(eth0, "mtu",       "1500\n");
            ramfs_mkfile(eth0, "type",      "1\n");
            inode_unref(eth0);
        }
        inode_unref(nd);
    }

    /* /sys/block/ */
    vfs_inode_t *bd = ramfs_mkdir_child(root, "block", MODE_DIR);
    if (bd) {
        vfs_inode_t *sda = ramfs_mkdir_child(bd, "sda", MODE_DIR);
        if (sda) {
            ramfs_mkfile(sda, "size",       "0\n");
            ramfs_mkfile(sda, "removable",  "0\n");
            inode_unref(sda);
        }
        inode_unref(bd);
    }
    return root;
}

/* ════════════════════════════════════════════════════════════
 *  VFS Core
 * ════════════════════════════════════════════════════════════ */

#define MOUNT_MAX 32

typedef struct {
    char         path[VFS_PATH_MAX];
    vfs_inode_t *root;
    int          used;
} mount_entry_t;

static vfs_inode_t *g_root    = NULL;
static mount_entry_t g_mounts[MOUNT_MAX];
static int           g_nmounts = 0;
static vfs_fd_t      g_fds[FD_MAX];

int vfs_mount(const char *path, vfs_inode_t *root) {
    if (!path || path[0] != '/' || !root) return -EINVAL;
    for (int i = 0; i < g_nmounts; i++)
        if (g_mounts[i].used && kstrcmp(g_mounts[i].path, path) == 0)
            return -EEXIST;
    if (g_nmounts >= MOUNT_MAX) return -ENOSPC;
    kstrcpy(g_mounts[g_nmounts].path, path);
    g_mounts[g_nmounts].root = root;
    g_mounts[g_nmounts].used = 1;
    g_nmounts++;
    return 0;
}

int vfs_unmount(const char *path) {
    if (!path) return -EINVAL;
    for (int i = 0; i < g_nmounts; i++) {
        if (!g_mounts[i].used || kstrcmp(g_mounts[i].path, path) != 0)
            continue;
        for (int j = i; j < g_nmounts - 1; j++)
            g_mounts[j] = g_mounts[j + 1];
        g_nmounts--;
        kmemset(&g_mounts[g_nmounts], 0, sizeof(g_mounts[g_nmounts]));
        return 0;
    }
    return -ENOENT;
}

vfs_inode_t *vfs_root(void) { return g_root; }

/* ── Path resolution ─────────────────────────────────────── */

/* Check if path starts with mount prefix */
static vfs_inode_t *mount_for_path(const char *path) {
    vfs_inode_t *best = NULL;
    int best_len = 0;
    for (int i = 0; i < g_nmounts; i++) {
        if (!g_mounts[i].used) continue;
        const char *mp = g_mounts[i].path;
        int mplen = (int)kstrlen(mp);
        /* path must start with mp */
        int match = 1;
        for (int j = 0; j < mplen; j++) {
            if (path[j] != mp[j]) { match = 0; break; }
        }
        if (!match) continue;
        /* path[mplen] must be '/' or '\0' */
        if (path[mplen] != '/' && path[mplen] != '\0') continue;
        if (mplen > best_len) { best_len = mplen; best = g_mounts[i].root; }
    }
    return best;
}

/* Resolve a single path, following symlinks up to VFS_SYMLINK_MAX times.
 * nofollow: don't follow the LAST component if it's a symlink.            */
static vfs_inode_t *resolve_path(const char *path, int nofollow) {
    if (!path || path[0] != '/') return NULL;
    if (path[1] == '\0') return g_root;

    /* Check mount table first */
    vfs_inode_t *mroot = mount_for_path(path);

    char buf[VFS_PATH_MAX];
    kstrcpy(buf, path);

    int hops = 0;
restart:
    if (hops > VFS_SYMLINK_MAX) { errno = ELOOP; return NULL; }

    /* Find which root to start from */
    vfs_inode_t *cur;
    const char *rel;

    /* Re-check mount table */
    mroot = mount_for_path(buf);
    if (mroot) {
        /* find how long the prefix is */
        int best_len = 0;
        for (int i = 0; i < g_nmounts; i++) {
            if (!g_mounts[i].used) continue;
            int mplen = (int)kstrlen(g_mounts[i].path);
            vfs_inode_t *mroot2 = mount_for_path(buf);
            if (mroot2 == g_mounts[i].root && mplen > best_len)
                best_len = mplen;
        }
        cur = mroot;
        rel = buf + best_len;
        if (*rel == '/') rel++;
    } else {
        cur = g_root;
        rel = buf + 1;
    }

    char comp[VFS_NAME_MAX + 1];

    while (*rel) {
        /* Extract component */
        int ci = 0;
        const char *p = rel;
        while (*p && *p != '/' && ci < VFS_NAME_MAX)
            comp[ci++] = *p++;
        comp[ci] = '\0';
        rel = (*p == '/') ? p + 1 : p;
        if (ci == 0) continue;

        /* "." — stay */
        if (comp[0] == '.' && comp[1] == '\0') continue;

        /* ".." — handled by dentry, fall through to lookup */

        /* Last component with nofollow: stop before following symlink */
        int is_last = (*rel == '\0');

        if (!cur->ops || !cur->ops->lookup) { errno = ENOTDIR; return NULL; }

        vfs_inode_t *next = NULL;
        int r = cur->ops->lookup(cur, comp, &next);
        if (r != 0) { errno = ENOENT; return NULL; }

        if (S_ISLNK(next->mode) && !(is_last && nofollow)) {
            /* Follow symlink */
            char lbuf[VFS_PATH_MAX];
            if (!next->ops || !next->ops->readlink) {
                inode_unref(next); errno = EINVAL; return NULL;
            }
            next->ops->readlink(next, lbuf, sizeof(lbuf));
            inode_unref(next);
            hops++;
            if (lbuf[0] == '/') {
                /* absolute symlink */
                kstrcpy(buf, lbuf);
                /* append remaining path */
                if (*rel) {
                    kstrcat(buf, "/");
                    kstrcat(buf, rel);
                }
            } else {
                /* relative symlink: replace current component */
                /* find dir part of current path and append link + rest */
                char newbuf[VFS_PATH_MAX];
                /* just prepend the original path up to before this comp */
                kstrcpy(newbuf, buf);
                /* find last / */
                char *slash = newbuf;
                for (char *pp = newbuf; *pp; pp++) if (*pp == '/') slash = pp;
                if (slash > newbuf) *(slash+1) = '\0';
                else { slash[1] = '\0'; }
                kstrcat(newbuf, lbuf);
                if (*rel) { kstrcat(newbuf, "/"); kstrcat(newbuf, rel); }
                kstrcpy(buf, newbuf);
            }
            goto restart;
        }

        /* Check for mounted filesystem at this inode */
        if (next->mountpoint) {
            vfs_inode_t *mounted_root = next->mountpoint;
            inode_unref(next);
            cur = mounted_root;
        } else {
            cur = next;
        }
    }

    return cur;
}

vfs_inode_t *vfs_lookup(const char *path) {
    return resolve_path(path, 0);
}

vfs_inode_t *vfs_lookup_nofollow(const char *path) {
    return resolve_path(path, 1);
}

/* ── Path splitting ──────────────────────────────────────── */

static void path_split(const char *path, char *dir, char *base) {
    int len = (int)kstrlen(path);
    int last = 0;
    for (int i = 0; i < len; i++) if (path[i] == '/') last = i;
    if (last == 0) {
        kstrcpy(dir, "/");
        kstrcpy(base, path + 1);
    } else {
        for (int i = 0; i < last; i++) dir[i] = path[i];
        dir[last] = '\0';
        kstrcpy(base, path + last + 1);
    }
}

/* ── fd table ────────────────────────────────────────────── */

static int fd_alloc(void) {
    for (int i = 3; i < FD_MAX; i++)
        if (!g_fds[i].used) return i;
    errno = EMFILE;
    return -1;
}

/* ── POSIX open ──────────────────────────────────────────── */

int vfs_open(const char *path, uint32_t flags, uint32_t mode) {
    /* Normalise old 2-arg callers: if mode==0 and creating, use 0644 */
    if (!mode) mode = 0644;

    vfs_inode_t *inode = resolve_path(path, (flags & O_NOFOLLOW) ? 1 : 0);

    if (!inode) {
        if (!(flags & O_CREAT)) { errno = ENOENT; return -1; }
        /* Creating a directory entry needs write and search permission on it. */
        char dir[VFS_PATH_MAX], base[VFS_NAME_MAX+1];
        path_split(path, dir, base);
        vfs_inode_t *parent = resolve_path(dir, 0);
        if (!parent || !parent->ops || !parent->ops->create) {
            if (parent) inode_unref(parent); errno = ENOENT; return -1;
        }
        if (vfs_require(parent,PERM_W|PERM_X)<0) { inode_unref(parent); return -1; }
        int r = parent->ops->create(parent, base, (mode & ~S_IFMT) | S_IFREG, &inode);
        inode_unref(parent);
        if (r != 0) { errno = -r; return -1; }
        /* fs drivers that allocate through inode_alloc inherit these values;
         * set them here too for mount adapters which use their own allocator. */
        inode->uid=g_session_uid; inode->gid=g_session_gid;
    } else {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            inode_unref(inode); errno = EEXIST; return -1;
        }
        if (S_ISDIR(inode->mode) && (flags & (O_WRONLY | O_RDWR))) {
            inode_unref(inode); errno = EISDIR; return -1;
        }
        uint32_t wanted=0, acc=flags&O_ACCMODE;
        if (acc != O_WRONLY) wanted|=PERM_R;
        if (acc != O_RDONLY) wanted|=PERM_W;
        if (wanted && vfs_require(inode,wanted)<0) { inode_unref(inode); return -1; }
    }

    int fd = fd_alloc();
    if (fd < 0) { inode_unref(inode); return -1; }

    g_fds[fd].inode   = inode;
    g_fds[fd].flags   = flags;
    g_fds[fd].used    = 1;
    g_fds[fd].cloexec = (flags & O_CLOEXEC) ? 1 : 0;
    kstrcpy(g_fds[fd].path, path);

    if (flags & O_TRUNC) {
        if (inode->ops && inode->ops->truncate)
            inode->ops->truncate(inode, 0);
    }
    g_fds[fd].offset = (flags & O_APPEND) ? inode->size : 0;

    return fd;
}

void vfs_close(int fd) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) return;
    inode_unref(g_fds[fd].inode);
    kmemset(&g_fds[fd], 0, sizeof(g_fds[fd]));
}

int64_t vfs_read(int fd, void *buf, uint64_t size) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    vfs_fd_t *f = &g_fds[fd];
    if ((f->flags & O_ACCMODE) == O_WRONLY) { errno=EINVAL; return -1; }
    if (vfs_require(f->inode,PERM_R)<0) return -1;
    if (!f->inode->ops || !f->inode->ops->read) { errno=EINVAL; return -1; }
    int64_t n = f->inode->ops->read(f->inode, f->offset, buf, size);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

int64_t vfs_write(int fd, const void *buf, uint64_t size) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    vfs_fd_t *f = &g_fds[fd];
    if ((f->flags & O_ACCMODE) == O_RDONLY) { errno=EINVAL; return -1; }
    if (vfs_require(f->inode,PERM_W)<0) return -1;
    if (!f->inode->ops || !f->inode->ops->write) { errno=EINVAL; return -1; }
    if (f->flags & O_APPEND) f->offset = f->inode->size;
    int64_t n = f->inode->ops->write(f->inode, f->offset, buf, size);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

int64_t vfs_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    vfs_fd_t *f = &g_fds[fd];
    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)f->offset; break;
        case SEEK_END: base = (int64_t)f->inode->size; break;
        default: errno = EINVAL; return -1;
    }
    int64_t newoff = base + offset;
    if (newoff < 0) { errno = EINVAL; return -1; }
    f->offset = (uint64_t)newoff;
    return (int64_t)f->offset;
}

int vfs_dup(int fd) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    int newfd = fd_alloc();
    if (newfd < 0) return -1;
    g_fds[newfd] = g_fds[fd];
    inode_ref(g_fds[newfd].inode);
    return newfd;
}

int vfs_dup2(int fd, int newfd) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    if (newfd < 0 || newfd >= FD_MAX) { errno=EINVAL; return -1; }
    if (newfd == fd) return fd;
    if (g_fds[newfd].used) vfs_close(newfd);
    g_fds[newfd] = g_fds[fd];
    inode_ref(g_fds[newfd].inode);
    return newfd;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i) { errno = ENOENT; return -1; }
    int r = -1;
    if (i->ops && i->ops->stat) r = i->ops->stat(i, st);
    inode_unref(i);
    return r;
}

int vfs_fstat(int fd, vfs_stat_t *st) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) { errno=EINVAL; return -1; }
    vfs_inode_t *i = g_fds[fd].inode;
    if (!i->ops || !i->ops->stat) return -1;
    return i->ops->stat(i, st);
}

int vfs_lstat(const char *path, vfs_stat_t *st) {
    vfs_inode_t *i = resolve_path(path, 1);
    if (!i) { errno = ENOENT; return -1; }
    int r = -1;
    if (i->ops && i->ops->stat) r = i->ops->stat(i, st);
    inode_unref(i);
    return r;
}

int vfs_chmod(const char *path, uint32_t mode) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i) return -1;
    if (g_session_uid != 0 && g_session_uid != i->uid) { inode_unref(i); errno=EACCES; return -1; }
    int r = -EINVAL;
    if (i->ops && i->ops->chmod) r = i->ops->chmod(i, mode & 07777U);
    inode_unref(i);
    return r;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i) return -1;
    if (g_session_uid != 0) { inode_unref(i); errno=EACCES; return -1; }
    int r = -EINVAL;
    if (i->ops && i->ops->chown) r = i->ops->chown(i, uid, gid);
    inode_unref(i);
    return r;
}

int vfs_truncate(const char *path, uint64_t size) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i) return -1;
    if (vfs_require(i,PERM_W)<0) { inode_unref(i); return -1; }
    int r = -EINVAL;
    if (i->ops && i->ops->truncate) r = i->ops->truncate(i, size);
    inode_unref(i);
    return r;
}

int vfs_ftruncate(int fd, uint64_t size) {
    if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) return -1;
    vfs_inode_t *i = g_fds[fd].inode;
    if (vfs_require(i,PERM_W)<0) return -1;
    if (!i->ops || !i->ops->truncate) return -1;
    return i->ops->truncate(i, size);
}

int vfs_mkdir(const char *path, uint32_t mode) {
    char dir[VFS_PATH_MAX], base[VFS_NAME_MAX+1];
    path_split(path, dir, base);
    vfs_inode_t *parent = resolve_path(dir, 0);
    if (!parent) { errno = ENOENT; return -1; }
    if (!parent->ops || !parent->ops->mkdir) { inode_unref(parent); errno = ENOTDIR; return -1; }
    if (vfs_require(parent,PERM_W|PERM_X)<0) { inode_unref(parent); return -1; }
    int r = parent->ops->mkdir(parent, base, (mode & ~S_IFMT) | S_IFDIR, NULL);
    inode_unref(parent);
    return r;
}

int vfs_rmdir(const char *path) {
    char dir[VFS_PATH_MAX], base[VFS_NAME_MAX+1];
    path_split(path, dir, base);
    vfs_inode_t *parent = resolve_path(dir, 0);
    if (!parent) return -1;
    if (vfs_require(parent,PERM_W|PERM_X)<0) { inode_unref(parent); return -1; }
    int r = -EINVAL;
    if (parent->ops && parent->ops->rmdir)
        r = parent->ops->rmdir(parent, base);
    inode_unref(parent);
    return r;
}

int vfs_unlink(const char *path) {
    char dir[VFS_PATH_MAX], base[VFS_NAME_MAX+1];
    path_split(path, dir, base);
    vfs_inode_t *parent = resolve_path(dir, 0);
    if (!parent) return -1;
    if (vfs_require(parent,PERM_W|PERM_X)<0) { inode_unref(parent); return -1; }
    int r = -EINVAL;
    if (parent->ops && parent->ops->unlink)
        r = parent->ops->unlink(parent, base);
    inode_unref(parent);
    return r;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    char od[VFS_PATH_MAX], ob[VFS_NAME_MAX+1];
    char nd[VFS_PATH_MAX], nb[VFS_NAME_MAX+1];
    path_split(oldpath, od, ob);
    path_split(newpath, nd, nb);
    vfs_inode_t *old_dir = resolve_path(od, 0);
    vfs_inode_t *new_dir = resolve_path(nd, 0);
    if (!old_dir || !new_dir) { if(old_dir) inode_unref(old_dir); if(new_dir) inode_unref(new_dir); return -1; }
    if (vfs_require(old_dir,PERM_W|PERM_X)<0 || vfs_require(new_dir,PERM_W|PERM_X)<0) { inode_unref(old_dir); inode_unref(new_dir); return -1; }
    int r = -EINVAL;
    if (old_dir->ops && old_dir->ops->rename)
        r = old_dir->ops->rename(old_dir, ob, new_dir, nb);
    inode_unref(old_dir);
    inode_unref(new_dir);
    return r;
}

int vfs_link(const char *oldpath, const char *newpath) {
    vfs_inode_t *target = resolve_path(oldpath, 0);
    if (!target) return -1;
    char nd[VFS_PATH_MAX], nb[VFS_NAME_MAX+1];
    path_split(newpath, nd, nb);
    vfs_inode_t *new_dir = resolve_path(nd, 0);
    if (!new_dir) { inode_unref(target); return -1; }
    if (vfs_require(new_dir,PERM_W|PERM_X)<0) { inode_unref(target); inode_unref(new_dir); return -1; }
    int r = -EINVAL;
    if (new_dir->ops && new_dir->ops->link)
        r = new_dir->ops->link(new_dir, nb, target);
    inode_unref(target);
    inode_unref(new_dir);
    return r;
}

int vfs_symlink(const char *target, const char *linkpath) {
    char ld[VFS_PATH_MAX], lb[VFS_NAME_MAX+1];
    path_split(linkpath, ld, lb);
    vfs_inode_t *link_dir = resolve_path(ld, 0);
    if (!link_dir) return -1;
    if (vfs_require(link_dir,PERM_W|PERM_X)<0) { inode_unref(link_dir); return -1; }
    int r = -EINVAL;
    if (link_dir->ops && link_dir->ops->symlink)
        r = link_dir->ops->symlink(link_dir, lb, target);
    inode_unref(link_dir);
    return r;
}

int vfs_readlink(const char *path, char *buf, size_t sz) {
    vfs_inode_t *i = resolve_path(path, 1);
    if (!i) return -1;
    int r = -EINVAL;
    if (i->ops && i->ops->readlink) r = i->ops->readlink(i, buf, sz);
    inode_unref(i);
    return r;
}

int vfs_create(const char *path, uint32_t mode) {
    return vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0644);
}

/* ── Directory iteration ─────────────────────────────────── */

DIR_t *vfs_opendir(const char *path) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i || !S_ISDIR(i->mode)) { if(i) inode_unref(i); errno = ENOTDIR; return NULL; }
    if (vfs_require(i,PERM_R|PERM_X)<0) { inode_unref(i); return NULL; }
    DIR_t *d = (DIR_t *)kmalloc(sizeof(DIR_t));
    if (!d) return NULL;
    kstrcpy(d->path, path);
    d->index = 0;
    d->valid = 1;
    inode_unref(i);
    return d;
}

static vfs_dirent_t g_dirent_buf;

vfs_dirent_t *vfs_readdir_next(DIR_t *dir) {
    if (!dir || !dir->valid) return NULL;
    vfs_inode_t *i = resolve_path(dir->path, 0);
    if (!i || !i->ops || !i->ops->readdir) { if(i) inode_unref(i); return NULL; }
    if (vfs_require(i,PERM_R|PERM_X)<0) { inode_unref(i); return NULL; }
    int r = i->ops->readdir(i, dir->index, &g_dirent_buf);
    inode_unref(i);
    if (r != 0) return NULL;
    dir->index++;
    return &g_dirent_buf;
}

int vfs_fd_readdir(int fd,vfs_dirent_t *out){
    if(fd<0||fd>=FD_MAX||!out||!g_fds[fd].used||!g_fds[fd].inode) return -1;
    vfs_inode_t *inode=g_fds[fd].inode;
    if(!S_ISDIR(inode->mode)||!inode->ops||!inode->ops->readdir||vfs_require(inode,PERM_R|PERM_X)<0) return -1;
    if(g_fds[fd].offset>0xffffffffULL) return 0;
    if(inode->ops->readdir(inode,(uint32_t)g_fds[fd].offset,out)!=0) return 0;
    g_fds[fd].offset++;
    return 1;
}

void vfs_closedir(DIR_t *dir) {
    if (dir) kfree(dir);
}

/* Legacy readdir */
vfs_dirent_t *vfs_readdir(const char *path, uint32_t index) {
    vfs_inode_t *i = resolve_path(path, 0);
    if (!i || !i->ops || !i->ops->readdir) return NULL;
    int r = i->ops->readdir(i, index, &g_dirent_buf);
    inode_unref(i);
    return (r == 0) ? &g_dirent_buf : NULL;
}

int vfs_listdir(const char *path, char names[][VFS_NAME_MAX+1], int *count) {
    vfs_inode_t *dir = resolve_path(path, 0);
    if (!dir || !S_ISDIR(dir->mode)) { if(dir) inode_unref(dir); return -1; }
    if (vfs_require(dir,PERM_R|PERM_X)<0) { inode_unref(dir); return -1; }
    *count = 0;
    for (uint32_t i = 0; ; i++) {
        vfs_dirent_t de;
        if (!dir->ops || !dir->ops->readdir) break;
        if (dir->ops->readdir(dir, i, &de) != 0) break;
        kstrcpy(names[*count], de.name);
        if (de.d_type == DT_DIR) kstrcat(names[*count], "/");
        (*count)++;
        if (*count >= 256) break;
    }
    inode_unref(dir);
    return 0;
}

/* ════════════════════════════════════════════════════════════
 *  Population helpers
 * ════════════════════════════════════════════════════════════ */

static void populate_etc(vfs_inode_t *etc) {
    ramfs_mkfile(etc, "hostname",    "atmkoala\n");
    ramfs_mkfile(etc, "timezone",    "UTC\n");
    ramfs_mkfile(etc, "os-release",
        "NAME=\"atmkoala\"\n"
        "VERSION=\"0.5\"\n"
        "ID=atmkoala\n"
        "PRETTY_NAME=\"atmkoala 0.5 x86-64\"\n"
        "VERSION_ID=\"0.5\"\n"
        "ANSI_COLOR=\"1;36\"\n"
        "HOME_URL=\"https://atmkoala.local\"\n");
    ramfs_mkfile(etc, "fstab",
        "# /etc/fstab\n"
        "ramfs   /        ramfs  defaults     0 0\n"
        "proc    /proc    proc   defaults     0 0\n"
        "devfs   /dev     devfs  defaults     0 0\n"
        "sysfs   /sys     sysfs  ro           0 0\n"
        "tmpfs   /tmp     tmpfs  rw,nosuid    0 0\n"
        "tmpfs   /run     tmpfs  rw,nosuid    0 0\n");
    ramfs_mkfile(etc, "passwd",
        "root:x:0:0:root:/root:/bin/sh\n"
        "user:x:1000:1000::/home/user:/bin/sh\n");
    ramfs_mkfile(etc, "shadow",
        "root:*:19000:0:99999:7:::\n"
        "user:*:19000:0:99999:7:::\n");
    ramfs_mkfile(etc, "group",
        "root:x:0:\n"
        "sudo:x:27:user\n"
        "user:x:1000:\n"
        "disk:x:6:\n"
        "video:x:44:\n"
        "netdev:x:109:user\n");
    ramfs_mkfile(etc, "hosts",
        "127.0.0.1   localhost\n"
        "127.0.1.1   atmkoala\n"
        "::1         localhost ip6-localhost ip6-loopback\n");
    ramfs_mkfile(etc, "resolv.conf",
        "nameserver 8.8.8.8\nnameserver 1.1.1.1\n");
    ramfs_mkfile(etc, "motd",
        "atmkoala 0.5  |  x86-64  |  type 'help'\n");
    ramfs_mkfile(etc, "issue",
        "atmkoala 0.5 \\l\n");
    ramfs_mkfile(etc, "shells",
        "/bin/sh\n/bin/qsh\n");
    ramfs_mkfile(etc, "profile",
        "# /etc/profile\nexport PATH=/bin:/usr/bin:/usr/local/bin\n"
        "export HOME=/root\nexport TERM=vt100\n");

    /* /etc/network/ */
    vfs_inode_t *net = ramfs_mkdir_child(etc, "network", MODE_DIR);
    if (net) {
        ramfs_mkfile(net, "interfaces",
            "# /etc/network/interfaces\nauto lo\niface lo inet loopback\n"
            "\nauto eth0\niface eth0 inet dhcp\n");
        inode_unref(net);
    }

    /* /etc/sysctl.d/ */
    vfs_inode_t *sysctl = ramfs_mkdir_child(etc, "sysctl.d", MODE_DIR);
    if (sysctl) {
        ramfs_mkfile(sysctl, "99-atmkoala.conf",
            "net.ipv4.ip_forward = 0\n"
            "kernel.dmesg_restrict = 0\n");
        inode_unref(sysctl);
    }
}

static void populate_home(vfs_inode_t *home) {
    /* /home/user */
    vfs_inode_t *u = ramfs_mkdir_child(home, "user", MODE_DIR);
    if (!u) return;
    u->uid = 1000; u->gid = 1000;

    ramfs_mkfile(u, ".profile",
        "# ~/.profile\n[ -f ~/.shrc ] && . ~/.shrc\n");
    ramfs_mkfile(u, ".shrc",
        "# ~/.shrc\nexport PS1='user@atmkoala:~$ '\nalias ls='ls --color'\n");
    ramfs_mkfile(u, "readme.txt",
        "atmkoala home directory.\nType 'help' for commands.\n");

    vfs_inode_t *docs = ramfs_mkdir_child(u, "Documents", MODE_DIR);
    if (docs) inode_unref(docs);
    vfs_inode_t *dl = ramfs_mkdir_child(u, "Downloads", MODE_DIR);
    if (dl) inode_unref(dl);

    inode_unref(u);
}

static void populate_usr(vfs_inode_t *usr) {
    vfs_inode_t *bin = ramfs_mkdir_child(usr, "bin", MODE_DIR);
    if (bin) inode_unref(bin);
    vfs_inode_t *lib = ramfs_mkdir_child(usr, "lib", MODE_DIR);
    if (lib) inode_unref(lib);
    vfs_inode_t *share = ramfs_mkdir_child(usr, "share", MODE_DIR);
    if (share) {
        vfs_inode_t *man = ramfs_mkdir_child(share, "man", MODE_DIR);
        if (man) inode_unref(man);
        inode_unref(share);
    }
    vfs_inode_t *local = ramfs_mkdir_child(usr, "local", MODE_DIR);
    if (local) {
        vfs_inode_t *lb = ramfs_mkdir_child(local, "bin", MODE_DIR);
        if (lb) inode_unref(lb);
        inode_unref(local);
    }
}

static void populate_var(vfs_inode_t *var) {
    vfs_inode_t *log = ramfs_mkdir_child(var, "log", MODE_DIR);
    if (log) {
        ramfs_mkfile(log, "syslog",   "");
        ramfs_mkfile(log, "kern.log", "");
        ramfs_mkfile(log, "dmesg",    "");
        inode_unref(log);
    }
    vfs_inode_t *run = ramfs_mkdir_child(var, "run", MODE_DIR);
    if (run) {
        ramfs_mkfile(run, "utmp", "");
        inode_unref(run);
    }
    vfs_inode_t *cache = ramfs_mkdir_child(var, "cache", MODE_DIR);
    if (cache) inode_unref(cache);
    vfs_inode_t *lib  = ramfs_mkdir_child(var, "lib", MODE_DIR);
    if (lib) inode_unref(lib);
    vfs_inode_t *spool = ramfs_mkdir_child(var, "spool", MODE_DIR);
    if (spool) inode_unref(spool);
}

/* ════════════════════════════════════════════════════════════
 *  vfs_init
 * ════════════════════════════════════════════════════════════ */
void vfs_init(void) {
    kmemset(g_mounts, 0, sizeof(g_mounts));
    kmemset(g_fds,    0, sizeof(g_fds));
    g_nmounts = 0;

    /* Create root ramfs */
    g_root = ramfs_create_root();

    /* Standard POSIX directory hierarchy */
    const char *top_dirs[] = {
        "bin","boot","dev","etc","home","lib","lib64",
        "mnt","opt","proc","root","run","srv","sys","tmp",
        "usr","var", NULL
    };
    for (int i = 0; top_dirs[i]; i++)
        ramfs_mkdir_child(g_root, top_dirs[i], MODE_DIR);

    /* /root — root's home */
    vfs_inode_t *root_home = resolve_path("/root", 0);
    if (root_home) {
        root_home->mode = S_IFDIR | 0700;
        ramfs_mkfile(root_home, ".profile",
            "# /root/.profile\nexport PS1='root@atmkoala:~# '\n");
        inode_unref(root_home);
    }

    /* /etc */
    vfs_inode_t *etc = resolve_path("/etc", 0);
    if (etc) { populate_etc(etc); inode_unref(etc); }

    /* /home */
    vfs_inode_t *home = resolve_path("/home", 0);
    if (home) { populate_home(home); inode_unref(home); }

    /* /usr */
    vfs_inode_t *usr = resolve_path("/usr", 0);
    if (usr) { populate_usr(usr); inode_unref(usr); }

    /* /var */
    vfs_inode_t *var = resolve_path("/var", 0);
    if (var) { populate_var(var); inode_unref(var); }

    /* /tmp — sticky */
    vfs_inode_t *tmp = resolve_path("/tmp", 0);
    if (tmp) { tmp->mode = S_IFDIR | S_ISVTX | 0777; inode_unref(tmp); }

    /* /run */
    vfs_inode_t *run = resolve_path("/run", 0);
    if (run) { run->mode = S_IFDIR | 0755; inode_unref(run); }

    /* /lib64 → symlink to /lib */
    vfs_symlink("/lib", "/lib64");

    /* /bin/sh → stub symlink */
    ramfs_mkfile(resolve_path("/bin", 0), "sh",   "");
    vfs_symlink("/bin/sh", "/usr/bin/sh");

    /* Mount devfs at /dev */
    vfs_inode_t *devfs  = devfs_create();
    vfs_inode_t *dev_mp = resolve_path("/dev", 0);
    if (devfs && dev_mp) dev_mp->mountpoint = devfs;

    /* Mount procfs at /proc */
    vfs_inode_t *procfs  = procfs_create();
    vfs_inode_t *proc_mp = resolve_path("/proc", 0);
    if (procfs && proc_mp) proc_mp->mountpoint = procfs;

    /* Mount sysfs at /sys */
    vfs_inode_t *sysfs_root = sysfs_create();
    vfs_inode_t *sys_mp     = resolve_path("/sys", 0);
    if (sysfs_root && sys_mp) sys_mp->mountpoint = sysfs_root;

    /* stdin/stdout/stderr (fd 0,1,2) — point at /dev/null for kernel */
    vfs_inode_t *null_i = resolve_path("/dev/null", 0);
    if (null_i) {
        for (int i = 0; i < 3; i++) {
            g_fds[i].inode   = null_i;
            g_fds[i].flags   = (i == 0) ? O_RDONLY : O_WRONLY;
            g_fds[i].used    = 1;
            g_fds[i].offset  = 0;
            inode_ref(null_i);
        }
        inode_unref(null_i);
    }
}

/* ── fs registration (stub for future use) ──────────────── */
void vfs_register_fs(const char *name, vfs_fs_constructor_t ctor) {
    (void)name; (void)ctor;
}
