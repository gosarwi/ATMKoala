#ifndef ATM_BTRFS_H
#define ATM_BTRFS_H

#include <stdint.h>

#define BTRFS_SUPER_OFFSET 65536ULL
#define BTRFS_SUPER_SIZE   4096U
#define BTRFS_MIRROR_MAX   3

typedef struct {
    uint64_t offset, bytenr, generation;
    int present, magic_ok, csum_ok, geometry_ok;
} btrfs_mirror_t;

typedef struct {
    int valid, trusted, write_enabled, drive, part;
    int selected_mirror, mirror_count;
    uint32_t part_lba, sectorsize, nodesize, csum_type;
    uint64_t bytenr, generation, total_bytes, bytes_used, num_devices;
    uint8_t fsid[16];
    char label[257];
    btrfs_mirror_t mirrors[BTRFS_MIRROR_MAX];
} btrfs_state_t;

extern btrfs_state_t btrfs;
int btrfs_probe_partition(int drive,int part);
void btrfs_clear(void);
void btrfs_print_status(void);
/* Deliberately fail-closed until full CoW transaction, B-tree and checksum support exists. */
int btrfs_set_write_enabled(int enabled);
/* Writes a new filesystem label to all currently valid superblock mirrors.
 * This is a narrow, independently verifiable metadata transaction; it does
 * not claim general file/data write compatibility. */
int btrfs_label_set(const char *label);
const char *btrfs_last_error(void);
const char *btrfs_write_policy(void);

#endif
