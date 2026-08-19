#ifndef ATMKOALA_CATFS_VFS_H
#define ATMKOALA_CATFS_VFS_H

/* VFS adapter for the currently mounted CatFS volume. */
int catfs_vfs_mount(const char *mount_path);
int catfs_vfs_unmount(void);
int catfs_vfs_is_mounted(void);

#endif /* ATMKOALA_CATFS_VFS_H */
