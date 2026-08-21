#ifndef PARTMGR_H
#define PARTMGR_H
/*
 * partmgr.h — MBR partition table management for atmkoala
 *
 * The Master Boot Record partition table format is a documented,
 * vendor-neutral specification (originally IBM PC/MS-DOS, now an
 * open de-facto industry standard implemented identically by every
 * OS and described in countless public references) — there's no
 * single project's source to attribute or license here, it's a
 * fixed 512-byte on-disk byte layout.
 *
 *   Offset 0x000-0x1AD : bootstrap code (we leave it alone / zero it)
 *   Offset 0x1BE-0x1FD : 4 x 16-byte partition entries
 *   Offset 0x1FE-0x1FF : boot signature 0x55 0xAA
 *
 * Each partition entry:
 *   byte 0      : status (0x80 = active/bootable, 0x00 = inactive)
 *   byte 1-3    : CHS start (legacy, we don't use it — LBA only)
 *   byte 4      : partition type ID
 *   byte 5-7    : CHS end (legacy, unused)
 *   byte 8-11   : LBA of first sector (little-endian)
 *   byte 12-15  : number of sectors (little-endian)
 *
 * atmkoala partition type ID: 0xC5 (unused in the official list —
 * picking an unassigned ID avoids any other OS misidentifying a
 * CatFS partition as something it isn't).
 */
#include <stdint.h>

#define PART_TYPE_EMPTY    0x00
#define PART_TYPE_FAT32    0x0C
#define PART_TYPE_LINUX    0x83
#define PART_TYPE_LINUX_SWAP 0x82
#define PART_TYPE_GPT_PROTECTIVE 0xEE
#define PART_TYPE_CATFS    0xC5   /* atmkoala CatFS — unassigned upstream ID */

#define PART_MAX_ENTRIES   4
#define MBR_SIGNATURE_LO   0x55
#define MBR_SIGNATURE_HI   0xAA

typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_start[3];
    uint8_t  type;
    uint8_t  chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} mbr_entry_t;

typedef struct {
    mbr_entry_t entries[PART_MAX_ENTRIES];
    int         valid;       /* 1 if boot signature matched on read */
} mbr_table_t;

/* Read/parse the MBR from sector 0 of `drive` into `out` */
int  mbr_read(int drive, mbr_table_t *out);

/* Validate signatures-in-memory, status/type fields, non-empty ranges and
 * pairwise non-overlap. This does not yet check physical drive capacity. */
int  mbr_validate(const mbr_table_t *tbl);
/* Structural validation plus detected ATA capacity bounds for `drive`. */
int  mbr_validate_drive(int drive, const mbr_table_t *tbl);
int  mbr_selftest(void);

/* Write `tbl` back to sector 0 of `drive`, preserving the existing
 * bootstrap code (first 446 bytes) unless `zero_bootcode` is set */
int  mbr_write(int drive, const mbr_table_t *tbl, int zero_bootcode);

/* Create an empty/default table (no partitions, valid signature) */
void mbr_init_empty(mbr_table_t *tbl);

/* Add a partition occupying [lba_start, lba_start+sectors).
 * Returns the entry index (0-3) or -1 if no free slot / overlap. */
int  mbr_add_partition(mbr_table_t *tbl, uint8_t type,
                       uint32_t lba_start, uint32_t sectors, int bootable);

/* Remove partition at entry index `idx` */
int  mbr_remove_partition(mbr_table_t *tbl, int idx);

/* Check whether [lba_start, lba_start+sectors) overlaps any existing
 * partition in `tbl` (excluding `ignore_idx`, pass -1 to check all) */
int  mbr_overlaps(const mbr_table_t *tbl, uint32_t lba_start,
                  uint32_t sectors, int ignore_idx);

/* Human-readable name for a partition type byte */
const char *part_type_name(uint8_t type);
/* Non-destructive signature probe for one valid primary MBR partition.
 * Returns a stable label: ext2, CatFS, unknown or unreadable. */
const char *mbr_probe_filesystem(int drive,const mbr_entry_t *entry);

#endif /* PARTMGR_H */
