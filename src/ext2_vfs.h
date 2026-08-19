#ifndef ATM_EXT2_VFS_H
#define ATM_EXT2_VFS_H

/* Read-only VFS exposure for an already validated ext2_mount_partition(). */
int ext2_vfs_mount(const char *mount_path);
int ext2_vfs_unmount(void);
int ext2_vfs_is_mounted(void);

#endif
