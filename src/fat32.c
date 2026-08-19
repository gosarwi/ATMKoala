/* fat32.c — FAT32 read-only driver — atmkoala v0.5 */
#include "fat32.h"
#include "disk.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

fat32_state_t fat32 = {0};

static void fat32_read_sector(uint32_t lba, uint8_t *buf) {
    disk_read(fat32.drive, lba, 1, buf);
}

static uint32_t fat32_next_cluster(uint32_t cluster) {
    static uint8_t sec[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat32.fat_start + fat_offset / 512;
    uint32_t ent_offset = fat_offset % 512;
    fat32_read_sector(fat_sector, sec);
    uint32_t val;
    kmemcpy(&val, sec + ent_offset, 4);
    return val & 0x0FFFFFFF;
}

int fat32_mount(int drive) {
    static uint8_t buf[512];
    fat32.drive = drive;
    fat32_read_sector(0, buf);
    kmemcpy(&fat32.bpb, buf, sizeof(fat32_bpb_t));

    /* Validate */
    if (fat32.bpb.bytes_per_sector != 512) return -1;
    if (fat32.bpb.sectors_per_fat32 == 0)  return -2;

    fat32.fat_start  = fat32.bpb.reserved_sectors;
    fat32.data_start = fat32.fat_start
                     + fat32.bpb.num_fats * fat32.bpb.sectors_per_fat32;
    fat32.cluster_size = fat32.bpb.sectors_per_cluster * 512;
    fat32.mounted = 1;

    terminal_write("[fat32] Mounted: ");
    char tmp[12]; kstrcpy(tmp, fat32.bpb.volume_label); tmp[11]=0;
    terminal_writeln(tmp);
    return 0;
}

int fat32_readdir(uint32_t cluster, fat32_dirent_t *out, int max) {
    if (!fat32.mounted) return -1;
    static uint8_t buf[512];
    int count = 0;
    while (cluster < 0x0FFFFFF8 && count < max) {
        uint32_t lba = fat32.data_start
                     + (cluster - 2) * fat32.bpb.sectors_per_cluster;
        for (int s = 0; s < fat32.bpb.sectors_per_cluster && count < max; s++) {
            fat32_read_sector(lba + (uint32_t)s, buf);
            fat32_dirent_t *d = (fat32_dirent_t*)buf;
            for (int i = 0; i < 16 && count < max; i++, d++) {
                if (d->name[0] == 0x00) return count;  /* end */
                if ((uint8_t)d->name[0] == 0xE5) continue;  /* deleted */
                if (d->attr == FAT_ATTR_LFN) continue;  /* LFN entry */
                if (d->name[0] == '.') continue;
                kmemcpy(&out[count], d, sizeof(fat32_dirent_t));
                count++;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    return count;
}

int fat32_readfile(uint32_t cluster, uint32_t size, uint8_t *buf, uint32_t bufsz) {
    if (!fat32.mounted) return -1;
    static uint8_t sec[512];
    uint32_t read = 0;
    while (cluster < 0x0FFFFFF8 && read < size && read < bufsz) {
        uint32_t lba = fat32.data_start
                     + (cluster - 2) * fat32.bpb.sectors_per_cluster;
        for (int s = 0; s < fat32.bpb.sectors_per_cluster; s++) {
            fat32_read_sector(lba + (uint32_t)s, sec);
            uint32_t chunk = 512;
            if (read + chunk > bufsz)  chunk = bufsz - read;
            if (read + chunk > size)   chunk = size  - read;
            kmemcpy(buf + read, sec, chunk);
            read += chunk;
            if (read >= size || read >= bufsz) return (int)read;
        }
        cluster = fat32_next_cluster(cluster);
    }
    return (int)read;
}

int fat32_find(const char *path, fat32_dirent_t *out) {
    if (!fat32.mounted) return -1;
    static fat32_dirent_t entries[64];
    uint32_t cluster = fat32.bpb.root_cluster;
    /* Simple: search root dir only for now */
    int n = fat32_readdir(cluster, entries, 64);
    for (int i = 0; i < n; i++) {
        /* Build 8.3 name */
        char name[13]; int ni = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            name[ni++] = entries[i].name[j];
        if (entries[i].ext[0] != ' ') {
            name[ni++] = '.';
            for (int j = 0; j < 3 && entries[i].ext[j] != ' '; j++)
                name[ni++] = entries[i].ext[j];
        }
        name[ni] = 0;
        /* Convert to uppercase for compare */
        char cmp[13]; int ci = 0;
        for (const char *p = path; *p && ci < 12; p++, ci++)
            cmp[ci] = (*p>='a'&&*p<='z') ? *p-32 : *p;
        cmp[ci] = 0;
        if (!kstrcmp(name, cmp)) { kmemcpy(out, &entries[i], sizeof(*out)); return 0; }
    }
    return -1;
}
