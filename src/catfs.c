/* catfs.c — CatFS v2: journaled block filesystem for atmkoala
 *
 * Layout (512-byte sectors):
 *   0:       Superblock
 *   1:       Journal header
 *   2-9:     Journal log (8 sectors, ring buffer of records)
 *   10-?:    Inode table  (256 inodes x 64 bytes)
 *   ?-?:     Block bitmap (CATFS_BLOCKS_MAX/8 bytes)
 *   ?+:      Data blocks (512 bytes each)
 */
#include "catfs.h"
#include "disk.h"
#include "kmalloc.h"
#include "util.h"
#include "pit.h"
#include <stdint.h>
#include <stddef.h>

catfs_state_t catfs = {0};

#define SB_LBA              0
#define JOURNAL_HDR_LBA      1
#define JOURNAL_LOG_LBA      2
#define JOURNAL_LOG_SECTORS  8
#define INODE_TABLE_LBA     (JOURNAL_LOG_LBA + JOURNAL_LOG_SECTORS)
/* An inode is 151 bytes: only three fit in a 512-byte sector.  The trailing
 * 59 bytes in every sector are intentionally unused, so the layout must be
 * based on inode slots per sector rather than a dense byte-size estimate. */
#define INODES_PER_SECTOR   (SECTOR_SIZE / (uint32_t)sizeof(catfs_inode_t))
#define INODE_SECTORS       (((uint32_t)CATFS_INODE_MAX + INODES_PER_SECTOR - 1) / INODES_PER_SECTOR)
#define BITMAP_LBA          (INODE_TABLE_LBA + INODE_SECTORS)
#define BITMAP_SECTORS      ((CATFS_BLOCKS_MAX / 8 + SECTOR_SIZE - 1) / SECTOR_SIZE)
#define DATA_LBA            (BITMAP_LBA + BITMAP_SECTORS)

static int  bm_test(uint32_t b) { return (catfs.bitmap[b/8] >> (b%8)) & 1; }
static void bm_set (uint32_t b) { catfs.bitmap[b/8] |=  (uint8_t)(1u << (b%8)); }
static void bm_clr (uint32_t b) { catfs.bitmap[b/8] &= (uint8_t)~(1u << (b%8)); }

static int bm_alloc(void) {
    for (uint32_t i = 0; i < CATFS_BLOCKS_MAX; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            if (catfs.sb.free_blocks > 0) catfs.sb.free_blocks--;
            return (int)i;
        }
    }
    return -1;
}
static void bm_free(uint32_t b) {
    if (bm_test(b)) { bm_clr(b); catfs.sb.free_blocks++; }
}
static uint32_t block_lba(uint32_t blk) { return catfs.sb.data_start_lba + blk; }
static uint32_t catfs_now(void) { return pit_get_ticks() / 100; }

/* All on-disk access goes through these two wrappers so a partition
 * offset (set by catfs_mount_at / catfs_format_at) is applied
 * uniformly — every LBA elsewhere in this file is still expressed
 * relative to "start of this filesystem", exactly as before. */
static int dsk_read(uint32_t lba, uint8_t *buf) {
    return disk_read_sector(catfs.disk_drive, catfs.partition_offset + lba, buf);
}
static int dsk_write(uint32_t lba, const uint8_t *buf) {
    return disk_write_sector(catfs.disk_drive, catfs.partition_offset + lba, buf);
}

/* ── Journal ─────────────────────────────────────────────── */
static int journal_write_rec(const catfs_journal_rec_t *rec) {
    uint32_t slot = catfs.jh.head % CATFS_JOURNAL_SLOTS;
    uint32_t recs_per_sector = SECTOR_SIZE / (uint32_t)sizeof(catfs_journal_rec_t);
    uint32_t sector_in_log   = slot / recs_per_sector;
    uint32_t offset_in_sector = (slot % recs_per_sector) * (uint32_t)sizeof(catfs_journal_rec_t);

    uint8_t sec[SECTOR_SIZE];
    uint32_t lba = JOURNAL_LOG_LBA + sector_in_log;
    if (dsk_read(lba, sec) < 0) return -1;
    kmemcpy(sec + offset_in_sector, rec, sizeof(catfs_journal_rec_t));
    if (dsk_write(lba, sec) < 0) return -1;
    catfs.jh.head++;
    return 0;
}

static int journal_log(uint8_t type, uint32_t inode_idx, uint32_t a, uint32_t b) {
    catfs_journal_rec_t rec;
    kmemset(&rec, 0, sizeof(rec));
    rec.type = type; rec.inode_idx = inode_idx; rec.a = a; rec.b = b;
    rec.seq = ++catfs.next_seq;
    return journal_write_rec(&rec);
}

static int journal_flush_header(void) {
    uint8_t sec[SECTOR_SIZE];
    kmemset(sec, 0, SECTOR_SIZE);
    catfs.jh.magic = CATFS_MAGIC;
    kmemcpy(sec, &catfs.jh, sizeof(catfs_journal_hdr_t));
    return dsk_write(JOURNAL_HDR_LBA, sec);
}

static int journal_commit(void) {
    catfs.jh.committed_seq = catfs.next_seq;
    return journal_flush_header();
}

static int journal_check_torn(int *out_torn_count) {
    int torn = 0;
    uint32_t recs_per_sector = SECTOR_SIZE / (uint32_t)sizeof(catfs_journal_rec_t);
    uint8_t sec[SECTOR_SIZE];
    for (uint32_t slot = catfs.jh.committed_seq; slot < catfs.jh.head; slot++) {
        uint32_t s = slot % CATFS_JOURNAL_SLOTS;
        uint32_t sector_in_log = s / recs_per_sector;
        uint32_t offset = (s % recs_per_sector) * (uint32_t)sizeof(catfs_journal_rec_t);
        if (dsk_read(JOURNAL_LOG_LBA + sector_in_log, sec) < 0)
            continue;
        catfs_journal_rec_t *rec = (catfs_journal_rec_t *)(sec + offset);
        if (rec->type != CATFS_J_NONE) torn++;
    }
    if (out_torn_count) *out_torn_count = torn;
    return torn > 0 ? -1 : 0;
}

/* ── Sync (sector-precise) ──────────────────────────────────── */
static int sync_superblock(void) {
    uint8_t sec[SECTOR_SIZE];
    kmemset(sec, 0, SECTOR_SIZE);
    kmemcpy(sec, &catfs.sb, sizeof(catfs_super_t));
    return dsk_write(SB_LBA, sec);
}

static int sync_inode(int idx) {
    uint32_t inodes_per_sector = SECTOR_SIZE / (uint32_t)sizeof(catfs_inode_t);
    uint32_t sector = (uint32_t)idx / inodes_per_sector;
    uint32_t offset = ((uint32_t)idx % inodes_per_sector) * (uint32_t)sizeof(catfs_inode_t);
    uint32_t lba = INODE_TABLE_LBA + sector;
    uint8_t sec[SECTOR_SIZE];
    if (dsk_read(lba, sec) < 0) return -1;
    kmemcpy(sec + offset, &catfs.inodes[idx], sizeof(catfs_inode_t));
    return dsk_write(lba, sec);
}

static int sync_bitmap_region(uint32_t first_block, uint32_t last_block) {
    uint32_t first_byte = first_block / 8;
    uint32_t last_byte  = last_block  / 8;
    uint32_t first_sector = first_byte / SECTOR_SIZE;
    uint32_t last_sector  = last_byte  / SECTOR_SIZE;
    for (uint32_t s = first_sector; s <= last_sector; s++) {
        uint8_t sec[SECTOR_SIZE];
        kmemset(sec, 0, SECTOR_SIZE);
        uint32_t avail = (uint32_t)sizeof(catfs.bitmap) - s * SECTOR_SIZE;
        uint32_t n = avail < SECTOR_SIZE ? avail : SECTOR_SIZE;
        kmemcpy(sec, catfs.bitmap + s * SECTOR_SIZE, n);
        if (dsk_write(BITMAP_LBA + s, sec) < 0) return -1;
    }
    return 0;
}

int catfs_sync(void) {
    if (!catfs.mounted) return -1;
    if (sync_superblock() < 0) return -1;

    uint32_t inodes_per_sector = SECTOR_SIZE / (uint32_t)sizeof(catfs_inode_t);
    uint32_t inode_sectors = ((uint32_t)CATFS_INODE_MAX + inodes_per_sector - 1) / inodes_per_sector;
    for (uint32_t s = 0; s < inode_sectors; s++) {
        uint8_t sec[SECTOR_SIZE];
        kmemset(sec, 0, SECTOR_SIZE);
        uint32_t n = inodes_per_sector;
        if ((s + 1) * inodes_per_sector > (uint32_t)CATFS_INODE_MAX)
            n = (uint32_t)CATFS_INODE_MAX - s * inodes_per_sector;
        kmemcpy(sec, &catfs.inodes[s * inodes_per_sector], n * sizeof(catfs_inode_t));
        if (dsk_write(INODE_TABLE_LBA + s, sec) < 0) return -1;
    }
    if (sync_bitmap_region(0, CATFS_BLOCKS_MAX - 1) < 0) return -1;

    catfs.sb.clean_unmount = 1;
    if (sync_superblock() < 0) return -1;
    return journal_flush_header();
}

/* ── Format ──────────────────────────────────────────────── */
int catfs_format(int drive, const char *label) {
    return catfs_format_at(drive, 0, label);
}

int catfs_format_at(int drive, uint32_t lba_offset, const char *label) {
    if (drive < 0 || !disk_drives[drive].present) return -1;
    kmemset(&catfs, 0, sizeof(catfs));
    catfs.disk_drive = drive;
    catfs.partition_offset = lba_offset;

    catfs.sb.magic           = CATFS_MAGIC;
    catfs.sb.version         = 2;
    catfs.sb.total_blocks    = CATFS_BLOCKS_MAX;
    /* Block number 0 is the inode sentinel for "not allocated". Reserve it
     * permanently so every allocatable block has a non-zero on-disk number. */
    catfs.sb.free_blocks     = CATFS_BLOCKS_MAX - 1;
    catfs.sb.inode_count     = CATFS_INODE_MAX;
    catfs.sb.free_inodes     = CATFS_INODE_MAX;
    catfs.sb.data_start_lba  = DATA_LBA;
    catfs.sb.bitmap_lba      = BITMAP_LBA;
    catfs.sb.inode_table_lba = INODE_TABLE_LBA;
    catfs.sb.journal_lba     = JOURNAL_LOG_LBA;
    catfs.sb.clean_unmount   = 1;
    kstrcpy(catfs.sb.label, label ? label : "catfs");

    bm_set(0);

    catfs.jh.magic = CATFS_MAGIC;
    catfs.jh.head = 0;
    catfs.jh.committed_seq = 0;
    catfs.next_seq = 0;

    catfs_inode_t *root = &catfs.inodes[0];
    kmemset(root, 0, sizeof(*root));
    kstrcpy(root->name, "/");
    root->type   = CATFS_TYPE_DIR;
    root->perms  = CATFS_PERM_DEFAULT_DIR;
    root->parent = -1;
    root->ctime = root->mtime = root->atime = catfs_now();
    catfs.sb.free_inodes--;

    catfs.mounted = 1;
    return catfs_sync();
}

/* ── Mount ───────────────────────────────────────────────── */
int catfs_mount(int drive) {
    return catfs_mount_at(drive, 0);
}

int catfs_mount_at(int drive, uint32_t lba_offset) {
    if (drive < 0 || !disk_drives[drive].present) return -1;
    uint8_t sec[SECTOR_SIZE];

    catfs.disk_drive = drive;   /* must be set before any dsk_read/dsk_write call */
    catfs.partition_offset = lba_offset;

    if (dsk_read(SB_LBA, sec) < 0) return -1;
    kmemcpy(&catfs.sb, sec, sizeof(catfs_super_t));
    if (catfs.sb.magic != CATFS_MAGIC) return -1;

    if (dsk_read(JOURNAL_HDR_LBA, sec) < 0) return -1;
    kmemcpy(&catfs.jh, sec, sizeof(catfs_journal_hdr_t));

    uint32_t inodes_per_sector = SECTOR_SIZE / (uint32_t)sizeof(catfs_inode_t);
    uint32_t inode_sectors = ((uint32_t)CATFS_INODE_MAX + inodes_per_sector - 1) / inodes_per_sector;
    for (uint32_t s = 0; s < inode_sectors; s++) {
        if (dsk_read(catfs.sb.inode_table_lba + s, sec) < 0) return -1;
        uint32_t n = inodes_per_sector;
        if ((s + 1) * inodes_per_sector > (uint32_t)CATFS_INODE_MAX)
            n = (uint32_t)CATFS_INODE_MAX - s * inodes_per_sector;
        kmemcpy(&catfs.inodes[s * inodes_per_sector], sec, n * sizeof(catfs_inode_t));
    }

    uint32_t bitmap_sectors = ((uint32_t)sizeof(catfs.bitmap) + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (uint32_t s = 0; s < bitmap_sectors; s++) {
        if (dsk_read(catfs.sb.bitmap_lba + s, sec) < 0) return -1;
        uint32_t avail = (uint32_t)sizeof(catfs.bitmap) - s * SECTOR_SIZE;
        uint32_t n = avail < SECTOR_SIZE ? avail : SECTOR_SIZE;
        kmemcpy(catfs.bitmap + s * SECTOR_SIZE, sec, n);
    }

    catfs.next_seq = catfs.jh.committed_seq;

    int was_clean = catfs.sb.clean_unmount;
    catfs.sb.clean_unmount = 0;
    sync_superblock();
    catfs.mounted = 1;

    if (!was_clean) {
        int torn = 0;
        journal_check_torn(&torn);
        (void)torn; /* surfaced via catfs_fsck(), not auto-fixed on mount */
    }
    return 0;
}

/* ── fsck ────────────────────────────────────────────────── */
int catfs_fsck_at(int drive, uint32_t lba_offset, int repair) {
    catfs_state_t saved = catfs;
    int problems = 0;
    if (catfs_mount_at(drive,lba_offset) < 0) { catfs=saved; return -1; }

    uint8_t *seen = (uint8_t *)kmalloc(CATFS_BLOCKS_MAX / 8);
    if (seen) {
        kmemset(seen, 0, CATFS_BLOCKS_MAX / 8);
        for (int i = 0; i < CATFS_INODE_MAX; i++) {
            catfs_inode_t *in = &catfs.inodes[i];
            if (in->type == CATFS_TYPE_FREE) continue;
            for (int d = 0; d < CATFS_DIRECT_BLOCKS; d++)
                if (in->blocks[d]) seen[in->blocks[d]/8] |= (uint8_t)(1u << (in->blocks[d]%8));
            if (in->indirect)  seen[in->indirect/8]  |= (uint8_t)(1u << (in->indirect%8));
            if (in->dindirect) seen[in->dindirect/8] |= (uint8_t)(1u << (in->dindirect%8));
        }
        /* Block zero is permanently reserved as the on-disk null-block
         * sentinel. It is intentionally marked but never referenced by an
         * inode, so begin consistency checks at the first allocatable block. */
        for (uint32_t b = 1; b < CATFS_BLOCKS_MAX; b++) {
            int marked = (catfs.bitmap[b/8] >> (b%8)) & 1;
            int actual = (seen[b/8] >> (b%8)) & 1;
            if (marked && !actual) { problems++; if (repair) bm_clr(b); }
            else if (!marked && actual) { problems++; if (repair) bm_set(b); }
        }
        kfree(seen);
    }

    for (int i = 1; i < CATFS_INODE_MAX; i++) {
        catfs_inode_t *in = &catfs.inodes[i];
        if (in->type == CATFS_TYPE_FREE) continue;
        if (in->parent < 0 || in->parent >= CATFS_INODE_MAX ||
            catfs.inodes[in->parent].type != CATFS_TYPE_DIR) {
            problems++;
            if (repair) { kmemset(in, 0, sizeof(*in)); catfs.sb.free_inodes++; }
        }
    }

    int torn = 0;
    journal_check_torn(&torn);
    problems += torn;
    if (repair && torn) {
        catfs.jh.head = catfs.jh.committed_seq;
        journal_flush_header();
    }

    if (repair && problems && catfs_sync()<0) problems++;
    /* fsck is an inspection/repair operation, not a mount operation. Preserve
     * the active CatFS context even when checking another MBR partition. */
    catfs = saved;
    return problems;
}

int catfs_fsck(int drive, int repair) {
    return catfs_fsck_at(drive,0,repair);
}

/* ── Path helpers ────────────────────────────────────────── */
static void split_last(const char *path, char *dir, char *base) {
    const char *last = path;
    for (const char *p = path; *p; p++) if (*p == '/') last = p;
    if (last == path) {
        kstrcpy(dir, "/"); kstrcpy(base, path + 1);
    } else {
        size_t n = (size_t)(last - path);
        kmemcpy(dir, path, n); dir[n] = 0;
        kstrcpy(base, last + 1);
    }
}

int catfs_lookup(const char *path) {
    if (!catfs.mounted) return -1;
    if (path[0] == '/' && path[1] == 0) return 0;
    int cur = 0;
    if (catfs.inodes[0].type != CATFS_TYPE_DIR) return -1;

    char comp[CATFS_NAME_MAX];
    const char *p = (path[0] == '/') ? path + 1 : path;
    while (*p) {
        int ci = 0;
        while (*p && *p != '/' && ci < CATFS_NAME_MAX - 1) comp[ci++] = *p++;
        comp[ci] = 0;
        if (*p == '/') p++;
        if (ci == 0) continue;
        int found = -1;
        for (int i = 1; i < CATFS_INODE_MAX; i++) {
            if (catfs.inodes[i].type == CATFS_TYPE_FREE) continue;
            if (catfs.inodes[i].parent == cur && kstrcmp(catfs.inodes[i].name, comp) == 0) {
                found = i; break;
            }
        }
        if (found < 0) return -1;
        cur = found;
    }
    return cur;
}

int catfs_create(const char *path, uint8_t type) {
    if (!catfs.mounted) return -1;
    char dir[64], base[CATFS_NAME_MAX];
    split_last(path, dir, base);
    int parent = catfs_lookup(dir);
    if (parent < 0) return -1;
    if (catfs_lookup(path) >= 0) return -1;

    for (int i = 1; i < CATFS_INODE_MAX; i++) {
        if (catfs.inodes[i].type != CATFS_TYPE_FREE) continue;
        journal_log(CATFS_J_CREATE, (uint32_t)i, (uint32_t)parent, type);

        kmemset(&catfs.inodes[i], 0, sizeof(catfs_inode_t));
        kstrcpy(catfs.inodes[i].name, base);
        catfs.inodes[i].type   = type;
        catfs.inodes[i].perms  = (type == CATFS_TYPE_DIR) ? CATFS_PERM_DEFAULT_DIR : CATFS_PERM_DEFAULT_FILE;
        catfs.inodes[i].parent = parent;
        catfs.inodes[i].ctime = catfs.inodes[i].mtime = catfs.inodes[i].atime = catfs_now();
        catfs.sb.free_inodes--;

        sync_inode(i);
        sync_superblock();
        journal_commit();
        return i;
    }
    return -1;
}

static void free_all_blocks(catfs_inode_t *in) {
    uint8_t sec[SECTOR_SIZE];
    for (int i = 0; i < CATFS_DIRECT_BLOCKS; i++)
        if (in->blocks[i]) { bm_free(in->blocks[i]); in->blocks[i] = 0; }

    if (in->indirect) {
        if (dsk_read(block_lba(in->indirect), sec) == 0) {
            uint32_t *ptrs = (uint32_t *)sec;
            for (uint32_t i = 0; i < CATFS_PTRS_PER_BLOCK; i++) if (ptrs[i]) bm_free(ptrs[i]);
        }
        bm_free(in->indirect); in->indirect = 0;
    }
    if (in->dindirect) {
        uint8_t l1[SECTOR_SIZE];
        if (dsk_read(block_lba(in->dindirect), l1) == 0) {
            uint32_t *l1ptrs = (uint32_t *)l1;
            for (uint32_t i = 0; i < CATFS_PTRS_PER_BLOCK; i++) {
                if (!l1ptrs[i]) continue;
                if (dsk_read(block_lba(l1ptrs[i]), sec) == 0) {
                    uint32_t *l2ptrs = (uint32_t *)sec;
                    for (uint32_t j = 0; j < CATFS_PTRS_PER_BLOCK; j++) if (l2ptrs[j]) bm_free(l2ptrs[j]);
                }
                bm_free(l1ptrs[i]);
            }
        }
        bm_free(in->dindirect); in->dindirect = 0;
    }
}
/* Release every allocated data block at or beyond `first`, and release pointer
 * sectors only after their final child has gone. This closes the old direct-only
 * truncate leak without inventing sparse-file semantics. */
static int free_blocks_from(catfs_inode_t *in,uint32_t first){
    uint8_t sec[SECTOR_SIZE],l1[SECTOR_SIZE];
    for(uint32_t i=first;i<CATFS_DIRECT_BLOCKS;i++)if(in->blocks[i]){bm_free(in->blocks[i]);in->blocks[i]=0;}
    if(in->indirect){
        if(dsk_read(block_lba(in->indirect),sec)<0)return -1;
        uint32_t *p=(uint32_t *)sec,start=first>CATFS_DIRECT_BLOCKS?first-CATFS_DIRECT_BLOCKS:0;int any=0;
        for(uint32_t i=0;i<CATFS_PTRS_PER_BLOCK;i++){if(i>=start&&p[i]){bm_free(p[i]);p[i]=0;}if(p[i])any=1;}
        if(any){if(dsk_write(block_lba(in->indirect),sec)<0)return -1;}else{bm_free(in->indirect);in->indirect=0;}
    }
    if(in->dindirect){
        if(dsk_read(block_lba(in->dindirect),l1)<0)return -1;
        uint32_t *p1=(uint32_t *)l1;int any_l1=0;uint32_t base0=CATFS_DIRECT_BLOCKS+CATFS_PTRS_PER_BLOCK;
        for(uint32_t i=0;i<CATFS_PTRS_PER_BLOCK;i++){
            if(!p1[i])continue;uint32_t base=base0+i*CATFS_PTRS_PER_BLOCK;
            if(first>=base+CATFS_PTRS_PER_BLOCK){any_l1=1;continue;}
            if(dsk_read(block_lba(p1[i]),sec)<0)return -1;
            uint32_t *p2=(uint32_t *)sec,start=first>base?first-base:0;int any_l2=0;
            for(uint32_t j=0;j<CATFS_PTRS_PER_BLOCK;j++){if(j>=start&&p2[j]){bm_free(p2[j]);p2[j]=0;}if(p2[j])any_l2=1;}
            if(any_l2){if(dsk_write(block_lba(p1[i]),sec)<0)return -1;any_l1=1;}else{bm_free(p1[i]);p1[i]=0;}
        }
        if(any_l1){if(dsk_write(block_lba(in->dindirect),l1)<0)return -1;}else{bm_free(in->dindirect);in->dindirect=0;}
    }
    return 0;
}

int catfs_unlink(int idx) {
    if (!catfs.mounted || idx <= 0 || idx >= CATFS_INODE_MAX) return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE) return -1;
    /* Never orphan children by removing a non-empty directory. */
    if (in->type == CATFS_TYPE_DIR) {
        for (int i = 1; i < CATFS_INODE_MAX; i++)
            if (catfs.inodes[i].type != CATFS_TYPE_FREE &&
                catfs.inodes[i].parent == idx)
                return -1;
    }

    journal_log(CATFS_J_UNLINK, (uint32_t)idx, 0, 0);
    free_all_blocks(in);
    kmemset(in, 0, sizeof(catfs_inode_t));
    catfs.sb.free_inodes++;

    sync_inode(idx);
    sync_bitmap_region(0, CATFS_BLOCKS_MAX - 1);
    sync_superblock();
    journal_commit();
    return 0;
}

int catfs_rename(int idx, int new_parent, const char *new_name) {
    if (!catfs.mounted || !new_name || idx <= 0 || idx >= CATFS_INODE_MAX)
        return -1;
    if (new_parent < 0 || new_parent >= CATFS_INODE_MAX ||
        catfs.inodes[new_parent].type != CATFS_TYPE_DIR)
        return -1;
    size_t name_len = kstrlen(new_name);
    if (name_len == 0 || name_len >= CATFS_NAME_MAX)
        return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE)
        return -1;
    /* A directory cannot be moved into itself or one of its descendants. */
    for (int p = new_parent; p >= 0; p = catfs.inodes[p].parent)
        if (p == idx) return -1;
    for (int i = 1; i < CATFS_INODE_MAX; i++) {
        if (i == idx || catfs.inodes[i].type == CATFS_TYPE_FREE) continue;
        if (catfs.inodes[i].parent == new_parent &&
            kstrcmp(catfs.inodes[i].name, new_name) == 0)
            return -1;
    }
    journal_log(CATFS_J_WRITE_META, (uint32_t)idx,
                (uint32_t)in->parent, (uint32_t)new_parent);
    in->parent = new_parent;
    kstrcpy(in->name, new_name);
    in->mtime = catfs_now();
    if (sync_inode(idx) < 0 || sync_superblock() < 0) return -1;
    return journal_commit();
}

int catfs_chmod(int idx, uint16_t perms) {
    if (!catfs.mounted || idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE) return -1;
    journal_log(CATFS_J_WRITE_META, (uint32_t)idx, in->perms, perms & 0777u);
    in->perms = (uint16_t)(perms & 0777u);
    in->mtime = catfs_now();
    if (sync_inode(idx) < 0 || sync_superblock() < 0) return -1;
    return journal_commit();
}

static int resolve_block(catfs_inode_t *in, uint32_t blk_i, int create, uint32_t *out_block) {
    if (blk_i < CATFS_DIRECT_BLOCKS) {
        if (!in->blocks[blk_i]) {
            if (!create) return -1;
            int nb = bm_alloc(); if (nb < 0) return -1;
            in->blocks[blk_i] = (uint32_t)nb;
        }
        *out_block = in->blocks[blk_i];
        return 0;
    }
    blk_i -= CATFS_DIRECT_BLOCKS;
    if (blk_i < CATFS_PTRS_PER_BLOCK) {
        if (!in->indirect) {
            if (!create) return -1;
            int nb = bm_alloc(); if (nb < 0) return -1;
            in->indirect = (uint32_t)nb;
            uint8_t zero[SECTOR_SIZE]; kmemset(zero, 0, SECTOR_SIZE);
            dsk_write(block_lba(in->indirect), zero);
        }
        uint8_t sec[SECTOR_SIZE];
        if (dsk_read(block_lba(in->indirect), sec) < 0) return -1;
        uint32_t *ptrs = (uint32_t *)sec;
        if (!ptrs[blk_i]) {
            if (!create) return -1;
            int nb = bm_alloc(); if (nb < 0) return -1;
            ptrs[blk_i] = (uint32_t)nb;
            if (dsk_write(block_lba(in->indirect), sec) < 0) return -1;
        }
        *out_block = ptrs[blk_i];
        return 0;
    }
    blk_i -= CATFS_PTRS_PER_BLOCK;
    uint32_t l1_idx = blk_i / CATFS_PTRS_PER_BLOCK;
    uint32_t l2_idx = blk_i % CATFS_PTRS_PER_BLOCK;
    if (l1_idx >= CATFS_PTRS_PER_BLOCK) return -1;

    if (!in->dindirect) {
        if (!create) return -1;
        int nb = bm_alloc(); if (nb < 0) return -1;
        in->dindirect = (uint32_t)nb;
        uint8_t zero[SECTOR_SIZE]; kmemset(zero, 0, SECTOR_SIZE);
        dsk_write(block_lba(in->dindirect), zero);
    }
    uint8_t l1[SECTOR_SIZE];
    if (dsk_read(block_lba(in->dindirect), l1) < 0) return -1;
    uint32_t *l1ptrs = (uint32_t *)l1;
    if (!l1ptrs[l1_idx]) {
        if (!create) return -1;
        int nb = bm_alloc(); if (nb < 0) return -1;
        l1ptrs[l1_idx] = (uint32_t)nb;
        if (dsk_write(block_lba(in->dindirect), l1) < 0) return -1;
        uint8_t zero[SECTOR_SIZE]; kmemset(zero, 0, SECTOR_SIZE);
        dsk_write(block_lba(l1ptrs[l1_idx]), zero);
    }
    uint8_t l2[SECTOR_SIZE];
    if (dsk_read(block_lba(l1ptrs[l1_idx]), l2) < 0) return -1;
    uint32_t *l2ptrs = (uint32_t *)l2;
    if (!l2ptrs[l2_idx]) {
        if (!create) return -1;
        int nb = bm_alloc(); if (nb < 0) return -1;
        l2ptrs[l2_idx] = (uint32_t)nb;
        if (dsk_write(block_lba(l1ptrs[l1_idx]), l2) < 0) return -1;
    }
    *out_block = l2ptrs[l2_idx];
    return 0;
}

int catfs_read(int idx, uint32_t offset, uint8_t *buf, uint32_t len) {
    if (!catfs.mounted || idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE) return -1;
    if (offset >= in->size) return 0;
    if (offset + len > in->size) len = in->size - offset;

    uint32_t done = 0;
    uint8_t tmp[SECTOR_SIZE];
    while (done < len) {
        uint32_t blk_i   = (offset + done) / CATFS_BLOCK_SIZE;
        uint32_t blk_off = (offset + done) % CATFS_BLOCK_SIZE;
        uint32_t phys;
        if (resolve_block(in, blk_i, 0, &phys) < 0) break;
        if (dsk_read(block_lba(phys), tmp) < 0) break;
        uint32_t avail = CATFS_BLOCK_SIZE - blk_off;
        uint32_t chunk = len - done; if (chunk > avail) chunk = avail;
        kmemcpy(buf + done, tmp + blk_off, chunk);
        done += chunk;
    }
    in->atime = catfs_now();
    return (int)done;
}

int catfs_write(int idx, uint32_t offset, const uint8_t *buf, uint32_t len) {
    if (!catfs.mounted || idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE || in->type == CATFS_TYPE_DIR) return -1;

    uint32_t written = 0;
    uint8_t tmp[SECTOR_SIZE];
    uint32_t max_blocks = CATFS_MAX_FILE_BLOCKS;
    while (written < len) {
        uint32_t blk_i   = (offset + written) / CATFS_BLOCK_SIZE;
        uint32_t blk_off = (offset + written) % CATFS_BLOCK_SIZE;
        if (blk_i >= max_blocks) break;
        uint32_t phys;
        if (resolve_block(in, blk_i, 1, &phys) < 0) break;
        if (blk_off != 0 || (len - written) < CATFS_BLOCK_SIZE) {
            if (dsk_read(block_lba(phys), tmp) < 0)
                kmemset(tmp, 0, SECTOR_SIZE);
        }
        uint32_t avail = CATFS_BLOCK_SIZE - blk_off;
        uint32_t chunk = len - written; if (chunk > avail) chunk = avail;
        kmemcpy(tmp + blk_off, buf + written, chunk);
        if (dsk_write(block_lba(phys), tmp) < 0) break;
        written += chunk;
    }

    uint32_t end = offset + written;
    if (written > 0) {
        journal_log(CATFS_J_WRITE_META, (uint32_t)idx, in->size, end);
        if (end > in->size) in->size = end;
        in->mtime = catfs_now();
        sync_inode(idx);
        sync_superblock();
        sync_bitmap_region(0, CATFS_BLOCKS_MAX - 1);
        journal_commit();
    }
    return (int)written;
}

int catfs_truncate(int idx, uint32_t new_size) {
    if (!catfs.mounted || idx < 0 || idx >= CATFS_INODE_MAX) return -1;
    catfs_inode_t *in = &catfs.inodes[idx];
    if (in->type == CATFS_TYPE_FREE || in->type == CATFS_TYPE_DIR) return -1;

    if(journal_log(CATFS_J_WRITE_META, (uint32_t)idx, in->size, new_size)<0)return -1;
    if (new_size < in->size) {
        uint32_t first_freed = (new_size + CATFS_BLOCK_SIZE - 1) / CATFS_BLOCK_SIZE;
        if(free_blocks_from(in,first_freed)<0)return -1;
    }
    in->size = new_size;
    in->mtime = catfs_now();

    if(sync_inode(idx)<0||sync_superblock()<0||
       sync_bitmap_region(0, CATFS_BLOCKS_MAX - 1)<0)return -1;
    return journal_commit();
}

int catfs_listdir(int dir_idx, char names[][CATFS_NAME_MAX], int *count) {
    if (!catfs.mounted) return -1;
    *count = 0;
    for (int i = 0; i < CATFS_INODE_MAX; i++) {
        if (catfs.inodes[i].type == CATFS_TYPE_FREE) continue;
        if (catfs.inodes[i].parent != dir_idx) continue;
        kstrcpy(names[*count], catfs.inodes[i].name);
        if (catfs.inodes[i].type == CATFS_TYPE_DIR) kstrcat(names[*count], "/");
        (*count)++;
    }
    return 0;
}

int catfs_path_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len) {
    int idx = catfs_lookup(path);
    if (idx < 0) return -1;
    return catfs_read(idx, off, buf, len);
}
int catfs_path_write(const char *path, uint32_t off, const uint8_t *buf, uint32_t len) {
    int idx = catfs_lookup(path);
    if (idx < 0) idx = catfs_create(path, CATFS_TYPE_FILE);
    if (idx < 0) return -1;
    return catfs_write(idx, off, buf, len);
}
int catfs_path_create(const char *path) { return catfs_create(path, CATFS_TYPE_FILE); }
int catfs_path_mkdir (const char *path) { return catfs_create(path, CATFS_TYPE_DIR);  }
int catfs_path_unlink(const char *path) { return catfs_unlink(catfs_lookup(path)); }
int catfs_path_size(const char *path) {
    int idx = catfs_lookup(path);
    if (idx < 0) return -1;
    return (int)catfs.inodes[idx].size;
}
