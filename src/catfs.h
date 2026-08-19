#ifndef CATFS_H
#define CATFS_H

/*  CatFS v2 — journaled block filesystem for atmkoala
 *
 *  Improvements over v1:
 *    - Indirect blocks: files can grow far past the old 64-block
 *      (32KB) hard ceiling. 16 direct blocks + 1 indirect block
 *      (128 pointers) + 1 double-indirect (128*128 pointers) gives a
 *      max file size of roughly 16 + 128 + 16384 = 16528 blocks
 *      (~8.4MB at 512B/block), tunable via CATFS_BLOCK_SIZE.
 *    - Write-ahead journal: every metadata-mutating operation
 *      (create/unlink/write-that-changes-size/truncate) is described
 *      by a small journal record written and fsync'd BEFORE the real
 *      on-disk structures are touched. On mount, if the journal has
 *      uncommitted records, they are replayed (or rolled back,
 *      whichever is cheaper for the op) so a power-loss mid-write
 *      can't leave the inode table and the bitmap disagreeing with
 *      each other — the classic way small flat filesystems get
 *      silently corrupted on laptops and SBCs that lose power
 *      ungracefully.
 *    - Real timestamps (32-bit seconds, monotonic since boot epoch)
 *      and full 9-bit POSIX-style rwx permissions instead of the old
 *      3-bit owner-only field.
 *    - Per-operation sync: catfs_write() no longer rewrites the entire
 *      inode table + bitmap on every call. Only the journal entry and
 *      the touched inode/bitmap sectors are written, and a single
 *      explicit catfs_sync() flushes the superblock + journal commit
 *      record at the end of a batch.
 *
 *  On-disk layout (sectors, 512 bytes each):
 *    0:         Superblock
 *    1:         Journal header
 *    2-9:       Journal log (8 sectors, ring buffer of records)
 *    10-17:     Inode table   (256 inodes x 32 bytes = 8192B = 16 sectors... see below)
 *    18-25:     Block bitmap  (1 bit per block)
 *    26+:       Data blocks   (512 bytes each)
 */

#include <stdint.h>
#include <stddef.h>

#define CATFS_MAGIC          0xCAFE4002u   /* v2 magic, distinct from v1 */
#define CATFS_BLOCK_SIZE     512
#define CATFS_INODE_MAX      256
#define CATFS_BLOCKS_MAX     16384         /* 8MB at 512B blocks */
#define CATFS_NAME_MAX       56

#define CATFS_DIRECT_BLOCKS    16
#define CATFS_PTRS_PER_BLOCK  (CATFS_BLOCK_SIZE / 4)   /* 128 x uint32 */
#define CATFS_MAX_FILE_BLOCKS (CATFS_DIRECT_BLOCKS + CATFS_PTRS_PER_BLOCK \
                              + CATFS_PTRS_PER_BLOCK * CATFS_PTRS_PER_BLOCK)

#define CATFS_TYPE_FREE   0
#define CATFS_TYPE_FILE   1
#define CATFS_TYPE_DIR    2
#define CATFS_TYPE_SYMLINK 3

/* POSIX-style permission bits (owner/group/other, rwx each) */
#define CATFS_PERM_DEFAULT_FILE 0644
#define CATFS_PERM_DEFAULT_DIR  0755

/* ── Journal record types ────────────────────────────────── */
#define CATFS_J_NONE        0
#define CATFS_J_CREATE      1   /* inode_idx, parent, type      */
#define CATFS_J_UNLINK      2   /* inode_idx                    */
#define CATFS_J_WRITE_META  3   /* inode_idx, new_size          */
#define CATFS_J_BLOCK_ALLOC 4   /* inode_idx, block_index, lba  */
#define CATFS_J_COMMIT      0xFF /* marks the end of a clean op  */

typedef struct __attribute__((packed)) {
    uint8_t  type;          /* CATFS_J_* */
    uint8_t  _pad[3];
    uint32_t inode_idx;
    uint32_t a, b;           /* meaning depends on type */
    uint32_t seq;            /* monotonically increasing per-op sequence */
} catfs_journal_rec_t;

#define CATFS_JOURNAL_SLOTS  (8 * CATFS_BLOCK_SIZE / sizeof(catfs_journal_rec_t))

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* same as superblock magic, sanity check */
    uint32_t head;            /* next free slot index (ring buffer) */
    uint32_t committed_seq;   /* highest seq known fully committed */
    uint8_t  _pad[500];
} catfs_journal_hdr_t;

/* ── On-disk superblock ───────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t inode_count;
    uint32_t free_inodes;
    uint32_t data_start_lba;
    uint32_t bitmap_lba;
    uint32_t inode_table_lba;
    uint32_t journal_lba;
    uint32_t clean_unmount;   /* 1 if last unmount flushed cleanly */
    char     label[32];
    uint8_t  _pad[512 - 11*4 - 32];
} catfs_super_t;

/* ── On-disk inode (packed; currently 151 bytes) */
typedef struct __attribute__((packed)) {
    char     name[CATFS_NAME_MAX];
    uint8_t  type;
    uint16_t perms;            /* full 9-bit rwxrwxrwx, was 3-bit in v1 */
    uint32_t size;
    int32_t  parent;
    uint32_t ctime, mtime, atime;   /* seconds since boot-epoch */
    uint32_t blocks[CATFS_DIRECT_BLOCKS];
    uint32_t indirect;          /* block holding 128 more pointers */
    uint32_t dindirect;         /* block holding 128 indirect blocks */
} catfs_inode_t;

/* ── In-memory mounted state ──────────────────────────────── */
typedef struct {
    int             disk_drive;
    uint32_t        partition_offset; /* LBA added to every on-disk
                                        * access; 0 = whole-disk mode
                                        * (legacy/back-compat), nonzero
                                        * when mounted from a partition
                                        * created via partmgr */
    catfs_super_t     sb;
    catfs_journal_hdr_t jh;
    catfs_inode_t     inodes[CATFS_INODE_MAX];
    uint8_t         bitmap[CATFS_BLOCKS_MAX / 8];
    int             mounted;
    uint32_t        next_seq;
    int             dirty_inode_lo, dirty_inode_hi; /* range needing sync */
} catfs_state_t;

extern catfs_state_t catfs;

/* ── Public API (superset of v1, source-compatible) ──────── */
int catfs_format(int drive, const char *label);
int catfs_format_at(int drive, uint32_t lba_offset, const char *label);
int catfs_mount(int drive);
int catfs_mount_at(int drive, uint32_t lba_offset);
int catfs_sync(void);
int catfs_fsck(int drive, int repair);   /* new: consistency check */

int catfs_lookup(const char *path);
int catfs_create(const char *path, uint8_t type);
int catfs_unlink(int idx);
int catfs_rename(int idx, int new_parent, const char *new_name);
int catfs_chmod(int idx, uint16_t perms);
int catfs_read(int idx, uint32_t offset, uint8_t *buf, uint32_t len);
int catfs_write(int idx, uint32_t offset, const uint8_t *buf, uint32_t len);
int catfs_truncate(int idx, uint32_t new_size);
int catfs_listdir(int dir_idx, char names[][CATFS_NAME_MAX], int *count);

int catfs_path_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len);
int catfs_path_write(const char *path, uint32_t off, const uint8_t *buf, uint32_t len);
int catfs_path_create(const char *path);
int catfs_path_mkdir(const char *path);
int catfs_path_unlink(const char *path);
int catfs_path_size(const char *path);

#endif /* CATFS_H */
