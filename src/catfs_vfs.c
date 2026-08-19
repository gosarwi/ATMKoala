/* catfs_vfs.c — CatFS adapter for the generic VFS layer.
 *
 * CatFS keeps its own compact inode table on disk.  This adapter exposes
 * that table under one VFS mount point without duplicating file data in
 * RAMFS.  VFS does not own a filesystem-wide inode cache, so the adapter
 * uses pinned, static wrappers: CatFS supports at most 256 inodes and all
 * wrappers live for the duration of one mount.
 */
#include "catfs_vfs.h"
#include "catfs.h"
#include "vfs.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

#define CATFS_VFS_PINNED_REFS 0x40000000

static vfs_inode_t catfs_nodes[CATFS_INODE_MAX];
static int catfs_node_ready[CATFS_INODE_MAX];
static char catfs_mount_path[VFS_PATH_MAX];
static int catfs_vfs_active = 0;
static vfs_fsops_t catfs_vfs_ops;

static int catfs_vfs_index(const vfs_inode_t *node) {
    if (!node || !node->priv) return -1;
    return (int)((uintptr_t)node->priv - 1u);
}

static uint32_t catfs_vfs_mode(int idx) {
    const catfs_inode_t *in = &catfs.inodes[idx];
    uint32_t kind = S_IFREG;
    if (in->type == CATFS_TYPE_DIR) kind = S_IFDIR;
    else if (in->type == CATFS_TYPE_SYMLINK) kind = S_IFLNK;
    return kind | (uint32_t)(in->perms & 0777u);
}

static uint8_t catfs_vfs_dtype(int idx) {
    if (catfs.inodes[idx].type == CATFS_TYPE_DIR) return DT_DIR;
    if (catfs.inodes[idx].type == CATFS_TYPE_SYMLINK) return DT_LNK;
    return DT_REG;
}

static vfs_inode_t *catfs_vfs_inode(int idx) {
    if (!catfs.mounted || idx < 0 || idx >= CATFS_INODE_MAX ||
        catfs.inodes[idx].type == CATFS_TYPE_FREE)
        return NULL;
    vfs_inode_t *node = &catfs_nodes[idx];
    if (!catfs_node_ready[idx]) {
        kmemset(node, 0, sizeof(*node));
        node->ino = (uint64_t)idx + 1u;
        node->uid = 0;
        node->gid = 0;
        node->nlink = 1;
        node->ops = &catfs_vfs_ops;
        node->priv = (void *)(uintptr_t)((uint32_t)idx + 1u);
        /* Pinned wrappers must never be released through VFS inode_unref(). */
        node->refcount = CATFS_VFS_PINNED_REFS;
        catfs_node_ready[idx] = 1;
    }
    node->mode = catfs_vfs_mode(idx);
    node->size = catfs.inodes[idx].size;
    node->atime.sec = catfs.inodes[idx].atime;
    node->mtime.sec = catfs.inodes[idx].mtime;
    node->ctime.sec = catfs.inodes[idx].ctime;
    node->atime.nsec = node->mtime.nsec = node->ctime.nsec = 0;
    return node;
}

static int catfs_vfs_path_for_index(int idx, char *out, size_t outsz) {
    int chain[CATFS_INODE_MAX];
    int count = 0;
    if (!out || outsz < 2 || idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    while (idx > 0) {
        if (count >= CATFS_INODE_MAX || catfs.inodes[idx].type == CATFS_TYPE_FREE)
            return -1;
        chain[count++] = idx;
        idx = catfs.inodes[idx].parent;
        if (idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    }
    out[0] = '/'; out[1] = 0;
    size_t used = 1;
    for (int n = count - 1; n >= 0; n--) {
        const char *name = catfs.inodes[chain[n]].name;
        size_t len = kstrlen(name);
        if (used > 1) {
            if (used + 1 >= outsz) return -1;
            out[used++] = '/'; out[used] = 0;
        }
        if (used + len >= outsz) return -1;
        kmemcpy(out + used, name, len);
        used += len;
        out[used] = 0;
    }
    return 0;
}

static int catfs_vfs_child_path(int parent_idx, const char *name,
                                char *out, size_t outsz) {
    if (!name || !name[0] || kstrlen(name) >= CATFS_NAME_MAX) return -1;
    if (catfs_vfs_path_for_index(parent_idx, out, outsz) < 0) return -1;
    size_t used = kstrlen(out);
    size_t len = kstrlen(name);
    if (used > 1) {
        if (used + 1 >= outsz) return -1;
        out[used++] = '/';
    }
    if (used + len >= outsz) return -1;
    kmemcpy(out + used, name, len);
    out[used + len] = 0;
    return 0;
}

static int catfs_vfs_lookup(vfs_inode_t *dir, const char *name,
                            vfs_inode_t **out) {
    int parent = catfs_vfs_index(dir);
    if (!out || parent < 0 || catfs.inodes[parent].type != CATFS_TYPE_DIR)
        return -ENOTDIR;
    if (kstrcmp(name, ".") == 0) { *out = catfs_vfs_inode(parent); return 0; }
    if (kstrcmp(name, "..") == 0) {
        int up = catfs.inodes[parent].parent;
        *out = catfs_vfs_inode(up < 0 ? parent : up);
        return *out ? 0 : -ENOENT;
    }
    for (int i = 1; i < CATFS_INODE_MAX; i++) {
        if (catfs.inodes[i].type != CATFS_TYPE_FREE &&
            catfs.inodes[i].parent == parent &&
            kstrcmp(catfs.inodes[i].name, name) == 0) {
            *out = catfs_vfs_inode(i);
            return *out ? 0 : -ENOENT;
        }
    }
    return -ENOENT;
}

static int catfs_vfs_create_common(vfs_inode_t *dir, const char *name,
                                   uint32_t mode, uint8_t type,
                                   vfs_inode_t **out) {
    int parent = catfs_vfs_index(dir);
    char path[VFS_PATH_MAX];
    (void)mode;
    if (parent < 0 || catfs.inodes[parent].type != CATFS_TYPE_DIR) return -ENOTDIR;
    if (catfs_vfs_child_path(parent, name, path, sizeof(path)) < 0) return -ENAMETOOLONG;
    int idx = catfs_create(path, type);
    if (idx < 0) return -ENOSPC;
    if (out) *out = catfs_vfs_inode(idx);
    return 0;
}

static int catfs_vfs_create(vfs_inode_t *dir, const char *name,
                            uint32_t mode, vfs_inode_t **out) {
    return catfs_vfs_create_common(dir, name, mode, CATFS_TYPE_FILE, out);
}

static int catfs_vfs_mkdir(vfs_inode_t *dir, const char *name,
                           uint32_t mode, vfs_inode_t **out) {
    return catfs_vfs_create_common(dir, name, mode, CATFS_TYPE_DIR, out);
}

static int catfs_vfs_unlink(vfs_inode_t *dir, const char *name) {
    vfs_inode_t *node = NULL;
    int r = catfs_vfs_lookup(dir, name, &node);
    if (r < 0) return r;
    int idx = catfs_vfs_index(node);
    if (idx < 0 || catfs.inodes[idx].type == CATFS_TYPE_DIR) return -EISDIR;
    return catfs_unlink(idx) == 0 ? 0 : -EINVAL;
}

static int catfs_vfs_rmdir(vfs_inode_t *dir, const char *name) {
    vfs_inode_t *node = NULL;
    int r = catfs_vfs_lookup(dir, name, &node);
    if (r < 0) return r;
    int idx = catfs_vfs_index(node);
    if (idx <= 0 || catfs.inodes[idx].type != CATFS_TYPE_DIR) return -ENOTDIR;
    return catfs_unlink(idx) == 0 ? 0 : -ENOTEMPTY;
}

static int catfs_vfs_rename(vfs_inode_t *old_dir, const char *old_name,
                            vfs_inode_t *new_dir, const char *new_name) {
    vfs_inode_t *node = NULL;
    int r = catfs_vfs_lookup(old_dir, old_name, &node);
    if (r < 0) return r;
    int idx = catfs_vfs_index(node);
    int parent = catfs_vfs_index(new_dir);
    if (idx < 0 || parent < 0) return -EINVAL;
    return catfs_rename(idx, parent, new_name) == 0 ? 0 : -EINVAL;
}

static int catfs_vfs_symlink(vfs_inode_t *dir, const char *name,
                             const char *target) {
    vfs_inode_t *node = NULL;
    int r = catfs_vfs_create_common(dir, name, MODE_LINK, CATFS_TYPE_SYMLINK, &node);
    if (r < 0) return r;
    int idx = catfs_vfs_index(node);
    size_t len = kstrlen(target);
    if (len >= VFS_PATH_MAX || catfs_write(idx, 0, (const uint8_t *)target,
                                           (uint32_t)len) != (int)len) {
        catfs_unlink(idx);
        return -ENOSPC;
    }
    return 0;
}

static int catfs_vfs_readlink(vfs_inode_t *node, char *buf, size_t bufsz) {
    int idx = catfs_vfs_index(node);
    if (idx < 0 || !buf || bufsz == 0 ||
        catfs.inodes[idx].type != CATFS_TYPE_SYMLINK)
        return -EINVAL;
    int n = catfs_read(idx, 0, (uint8_t *)buf, (uint32_t)(bufsz - 1));
    if (n < 0) return -EINVAL;
    buf[n] = 0;
    return n;
}

static int catfs_vfs_readdir(vfs_inode_t *dir, uint32_t ordinal,
                             vfs_dirent_t *out) {
    int parent = catfs_vfs_index(dir);
    if (parent < 0 || !out || catfs.inodes[parent].type != CATFS_TYPE_DIR)
        return -ENOTDIR;
    uint32_t seen = 0;
    for (int i = 1; i < CATFS_INODE_MAX; i++) {
        if (catfs.inodes[i].type == CATFS_TYPE_FREE || catfs.inodes[i].parent != parent)
            continue;
        if (seen++ != ordinal) continue;
        kmemset(out, 0, sizeof(*out));
        out->ino = (uint64_t)i + 1u;
        out->inode = (uint32_t)i + 1u;
        out->d_type = catfs_vfs_dtype(i);
        out->type = out->d_type;
        kstrcpy(out->name, catfs.inodes[i].name);
        return 0;
    }
    return -ENOENT;
}

static int64_t catfs_vfs_read(vfs_inode_t *node, uint64_t off,
                              void *buf, uint64_t size) {
    int idx = catfs_vfs_index(node);
    if (idx < 0 || off > 0xFFFFFFFFu || size > 0xFFFFFFFFu) return -EINVAL;
    return catfs_read(idx, (uint32_t)off, (uint8_t *)buf, (uint32_t)size);
}

static int64_t catfs_vfs_write(vfs_inode_t *node, uint64_t off,
                               const void *buf, uint64_t size) {
    int idx = catfs_vfs_index(node);
    if (idx < 0 || off > 0xFFFFFFFFu || size > 0xFFFFFFFFu) return -EINVAL;
    int n = catfs_write(idx, (uint32_t)off, (const uint8_t *)buf, (uint32_t)size);
    if (n >= 0) (void)catfs_vfs_inode(idx);
    return n;
}

static int catfs_vfs_truncate(vfs_inode_t *node, uint64_t size) {
    int idx = catfs_vfs_index(node);
    if (idx < 0 || size > 0xFFFFFFFFu) return -EINVAL;
    int r = catfs_truncate(idx, (uint32_t)size);
    if (r == 0) (void)catfs_vfs_inode(idx);
    return r;
}

static int catfs_vfs_stat(vfs_inode_t *node, vfs_stat_t *st) {
    int idx = catfs_vfs_index(node);
    if (idx < 0 || !st || catfs.inodes[idx].type == CATFS_TYPE_FREE) return -ENOENT;
    const catfs_inode_t *in = &catfs.inodes[idx];
    kmemset(st, 0, sizeof(*st));
    st->st_ino = (uint64_t)idx + 1u;
    st->st_mode = catfs_vfs_mode(idx);
    st->st_nlink = 1;
    st->st_size = in->size;
    st->st_blksize = CATFS_BLOCK_SIZE;
    st->st_blocks = (in->size + CATFS_BLOCK_SIZE - 1u) / CATFS_BLOCK_SIZE;
    st->st_atime.sec = in->atime;
    st->st_mtime.sec = in->mtime;
    st->st_ctime.sec = in->ctime;
    st->inode = (uint32_t)idx + 1u;
    st->size = in->size;
    st->type = (in->type == CATFS_TYPE_DIR) ? VFS_DIR :
               (in->type == CATFS_TYPE_SYMLINK) ? VFS_SYMLINK : VFS_FILE;
    st->perms = (uint8_t)((in->perms >> 6) & 7u);
    return 0;
}

static int catfs_vfs_chmod(vfs_inode_t *node, uint32_t mode) {
    int idx = catfs_vfs_index(node);
    if (idx < 0) return -EINVAL;
    int r = catfs_chmod(idx, (uint16_t)(mode & 0777u));
    if (r == 0) (void)catfs_vfs_inode(idx);
    return r;
}

static int catfs_vfs_chown(vfs_inode_t *node, uint32_t uid, uint32_t gid) {
    (void)node; (void)uid; (void)gid;
    return -EINVAL; /* CatFS v2 has no uid/gid fields on disk. */
}

static vfs_fsops_t catfs_vfs_ops = {
    .lookup = catfs_vfs_lookup,
    .create = catfs_vfs_create,
    .mkdir = catfs_vfs_mkdir,
    .unlink = catfs_vfs_unlink,
    .rmdir = catfs_vfs_rmdir,
    .rename = catfs_vfs_rename,
    .link = NULL,
    .symlink = catfs_vfs_symlink,
    .readlink = catfs_vfs_readlink,
    .readdir = catfs_vfs_readdir,
    .read = catfs_vfs_read,
    .write = catfs_vfs_write,
    .truncate = catfs_vfs_truncate,
    .stat = catfs_vfs_stat,
    .chmod = catfs_vfs_chmod,
    .chown = catfs_vfs_chown,
    .destroy = NULL,
    .synth_read = NULL,
};

int catfs_vfs_mount(const char *mount_path) {
    if (!catfs.mounted || !mount_path || mount_path[0] != '/') return -1;
    if (catfs_vfs_active)
        return kstrcmp(catfs_mount_path, mount_path) == 0 ? 0 : -1;
    kmemset(catfs_nodes, 0, sizeof(catfs_nodes));
    kmemset(catfs_node_ready, 0, sizeof(catfs_node_ready));
    vfs_inode_t *root = catfs_vfs_inode(0);
    if (!root) return -1;
    if (vfs_mount(mount_path, root) < 0) return -1;
    kstrcpy(catfs_mount_path, mount_path);
    catfs_vfs_active = 1;
    return 0;
}

int catfs_vfs_unmount(void) {
    if (!catfs_vfs_active) return 0;
    if (vfs_unmount(catfs_mount_path) < 0) return -1;
    catfs_vfs_active = 0;
    catfs_mount_path[0] = 0;
    return 0;
}

int catfs_vfs_is_mounted(void) {
    return catfs_vfs_active;
}
