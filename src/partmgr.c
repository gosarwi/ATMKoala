/* partmgr.c — MBR partition table management for atmkoala */
#include "partmgr.h"
#include "disk.h"
#include "util.h"
#include <stdint.h>

int mbr_read(int drive, mbr_table_t *out) {
    uint8_t sec[SECTOR_SIZE];
    if (disk_read_sector(drive, 0, sec) < 0) {
        out->valid = 0;
        return -1;
    }
    out->valid = (sec[510] == MBR_SIGNATURE_LO && sec[511] == MBR_SIGNATURE_HI);
    for (int i = 0; i < PART_MAX_ENTRIES; i++) {
        kmemcpy(&out->entries[i], sec + 0x1BE + i * 16, sizeof(mbr_entry_t));
    }
    return out->valid ? 0 : -1;
}

int mbr_write(int drive, const mbr_table_t *tbl, int zero_bootcode) {
    uint8_t sec[SECTOR_SIZE];
    if (!tbl || mbr_validate_drive(drive,tbl) < 0) return -1;
    if (zero_bootcode) {
        kmemset(sec, 0, SECTOR_SIZE);
    } else {
        /* Preserve whatever bootstrap code is already on disk */
        if (disk_read_sector(drive, 0, sec) < 0) {
            kmemset(sec, 0, SECTOR_SIZE);
        }
    }
    for (int i = 0; i < PART_MAX_ENTRIES; i++) {
        kmemcpy(sec + 0x1BE + i * 16, &tbl->entries[i], sizeof(mbr_entry_t));
    }
    sec[510] = MBR_SIGNATURE_LO;
    sec[511] = MBR_SIGNATURE_HI;
    return disk_write_sector(drive, 0, sec);
}

int mbr_validate(const mbr_table_t *tbl) {
    if (!tbl) return -1;
    for (int i=0; i<PART_MAX_ENTRIES; i++) {
        const mbr_entry_t *e=&tbl->entries[i];
        if (e->type==PART_TYPE_EMPTY) {
            if (e->status || e->lba_start || e->sector_count) return -1;
            continue;
        }
        if (e->status!=0x00 && e->status!=0x80) return -1;
        if (!e->sector_count || e->lba_start<1 || e->lba_start>UINT32_MAX-e->sector_count) return -1;
        if (mbr_overlaps(tbl,e->lba_start,e->sector_count,i)) return -1;
    }
    return 0;
}

int mbr_validate_drive(int drive,const mbr_table_t *tbl) {
    if (mbr_validate(tbl)<0 || drive<0 || drive>=DISK_MAX_DRIVES ||
        !disk_drives[drive].present) return -1;
    uint64_t capacity=disk_drives[drive].sectors;
    if (!capacity) return -1;
    for (int i=0;i<PART_MAX_ENTRIES;i++) {
        const mbr_entry_t *e=&tbl->entries[i];
        if (e->type!=PART_TYPE_EMPTY && (uint64_t)e->lba_start+(uint64_t)e->sector_count>capacity) return -1;
    }
    return 0;
}

int mbr_selftest(void) {
    mbr_table_t t; mbr_init_empty(&t);
    if (mbr_add_partition(&t,PART_TYPE_CATFS,2048,4096,0)!=0 || mbr_validate(&t)<0) return -1;
    if (!mbr_overlaps(&t,4096,2048,-1)) return -1;
    t.entries[1].type=PART_TYPE_CATFS; t.entries[1].lba_start=UINT32_MAX-1; t.entries[1].sector_count=8;
    if (mbr_validate(&t)==0) return -1;
    mbr_init_empty(&t); t.entries[0].lba_start=1;
    if (mbr_validate(&t)==0) return -1;
    return 0;
}

void mbr_init_empty(mbr_table_t *tbl) {
    kmemset(tbl, 0, sizeof(*tbl));
    tbl->valid = 1;
}

int mbr_overlaps(const mbr_table_t *tbl, uint32_t lba_start,
                 uint32_t sectors, int ignore_idx) {
    if (!tbl || !sectors || lba_start > UINT32_MAX - sectors) return 1;
    uint32_t new_end = lba_start + sectors;
    for (int i = 0; i < PART_MAX_ENTRIES; i++) {
        if (i == ignore_idx) continue;
        const mbr_entry_t *e = &tbl->entries[i];
        if (e->type == PART_TYPE_EMPTY) continue;
        if (!e->sector_count || e->lba_start > UINT32_MAX - e->sector_count) return 1;
        uint32_t e_end = e->lba_start + e->sector_count;
        if (lba_start < e_end && new_end > e->lba_start) return 1;
    }
    return 0;
}

int mbr_add_partition(mbr_table_t *tbl, uint8_t type,
                      uint32_t lba_start, uint32_t sectors, int bootable) {
    if (lba_start < 1) return -1;          /* sector 0 reserved for MBR itself */
    if (mbr_overlaps(tbl, lba_start, sectors, -1)) return -1;

    for (int i = 0; i < PART_MAX_ENTRIES; i++) {
        if (tbl->entries[i].type != PART_TYPE_EMPTY) continue;
        tbl->entries[i].status       = bootable ? 0x80 : 0x00;
        tbl->entries[i].type         = type;
        tbl->entries[i].lba_start    = lba_start;
        tbl->entries[i].sector_count = sectors;
        kmemset(tbl->entries[i].chs_start, 0, 3);
        kmemset(tbl->entries[i].chs_end,   0, 3);
        return i;
    }
    return -1;
}

int mbr_remove_partition(mbr_table_t *tbl, int idx) {
    if (idx < 0 || idx >= PART_MAX_ENTRIES) return -1;
    kmemset(&tbl->entries[idx], 0, sizeof(mbr_entry_t));
    return 0;
}

const char *part_type_name(uint8_t type) {
    switch (type) {
        case PART_TYPE_EMPTY: return "empty";
        case PART_TYPE_FAT32: return "FAT32";
        case PART_TYPE_LINUX: return "Linux";
        case PART_TYPE_CATFS: return "CatFS";
        default:              return "unknown";
    }
}

const char *mbr_probe_filesystem(int drive,const mbr_entry_t *entry){
    uint8_t sec[SECTOR_SIZE];
    if(!entry || entry->type==PART_TYPE_EMPTY || !entry->sector_count) return "empty";
    if(drive<0 || drive>=DISK_MAX_DRIVES || !disk_drives[drive].present) return "unreadable";
    if((uint64_t)entry->lba_start+(uint64_t)entry->sector_count>(uint64_t)disk_drives[drive].sectors) return "invalid-range";
    if(disk_read_sector(drive,entry->lba_start,sec)<0) return "unreadable";
    uint32_t magic=(uint32_t)sec[0]|((uint32_t)sec[1]<<8)|((uint32_t)sec[2]<<16)|((uint32_t)sec[3]<<24);
    if(magic==0xCAFE4002u) return "CatFS";
    /* The ext2 superblock begins 1024 bytes from the partition start and its
     * little-endian s_magic is at byte 56 inside that superblock. */
    if(entry->sector_count>2 && disk_read_sector(drive,entry->lba_start+2,sec)==0 && sec[56]==0x53 && sec[57]==0xEF) return "ext2";
    return "unknown";
}
