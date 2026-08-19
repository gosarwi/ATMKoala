#ifndef FAT32_H
#define FAT32_H
/* fat32.h — FAT32 driver для atmkoala v0.5 */
#include <stdint.h>
#include <stddef.h>

#define FAT32_MAGIC  0xAA55
#define FAT_ATTR_DIR 0x10
#define FAT_ATTR_LFN 0x0F

typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;       /* 0 for FAT32 */
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t sectors_per_fat16;  /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
    uint32_t sectors_per_fat32;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved2;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_bpb_t;

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t size;
} fat32_dirent_t;

typedef struct {
    int      mounted;
    int      drive;
    fat32_bpb_t bpb;
    uint32_t fat_start;    /* LBA */
    uint32_t data_start;   /* LBA */
    uint32_t cluster_size; /* bytes */
} fat32_state_t;

extern fat32_state_t fat32;

int  fat32_mount(int drive);
int  fat32_readdir(uint32_t cluster, fat32_dirent_t *out, int max);
int  fat32_readfile(uint32_t cluster, uint32_t size, uint8_t *buf, uint32_t bufsz);
int  fat32_find(const char *path, fat32_dirent_t *out);

#endif
