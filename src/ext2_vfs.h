#ifndef ATM_EXT2_VFS_H
#define ATM_EXT2_VFS_H

/* Explicit VFS exposure for an already validated ext2_mount_partition().
 * Reads are normal VFS reads; writes retain ext2_write_range's guarded
 * existing-direct-block policy and require the Ext2 write guard. */
int ext2_vfs_mount(const char *mount_path);
int ext2_vfs_unmount(void);
int ext2_vfs_is_mounted(void);
int ext2_vfs_is_busy(void);

#endif
