#ifndef ATM_EXT2_H
#define ATM_EXT2_H

#include <stdint.h>
#include <stddef.h>

#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2

#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFIFO  0x1000
#define EXT2_S_IFSOCK 0xC000

typedef struct {
    int mounted, drive, part;
    uint32_t part_lba, block_size, blocks_per_group, inodes_per_group, inode_size;
    uint32_t group_desc_block, groups, blocks_count, inodes_count;
    uint32_t free_blocks, free_inodes, feature_incompat, feature_compat;
    uint16_t fs_state, fs_errors;
    int write_enabled;
    const char *last_error;
    uint8_t mbr_sig0, mbr_sig1;
    char volume_name[17];
} ext2_state_t;

typedef struct {
    uint32_t inode;
    uint16_t mode, links_count;
    uint32_t size, blocks;
    uint32_t block[15];
} ext2_inode_t;

extern ext2_state_t ext2;
int ext2_mount_partition(int drive,int part);
int ext2_unmount(void);
int ext2_lookup(const char *path,ext2_inode_t *out,uint8_t *type_out);
int ext2_stat(const char *path,ext2_inode_t *out,char *type_out);
int ext2_readdir(const char *path,char *out,size_t out_size);
int ext2_ls_long(const char *path,char *out,size_t out_size);
int ext2_readfile(const char *path,uint8_t *out,size_t out_size,size_t *read_out);
int ext2_read_range(const char *path,uint32_t offset,uint8_t *out,size_t out_size,size_t *read_out);
int ext2_readlink(const char *path,char *out,size_t out_size);
/* Guarded write: only clean, non-journaled EXT2 volumes; existing direct blocks;
 * no allocation, create, truncate, directory update or indirect metadata mutation. */
int ext2_set_write_enabled(int enabled);
int ext2_write_range(const char *path,uint32_t offset,const uint8_t *data,size_t len,size_t *written);
const char *ext2_last_error(void);
void ext2_print_status(void);
void ext2_print_info(void);

#endif
