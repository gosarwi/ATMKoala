#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <stddef.h>

/* ATA PIO register offsets from I/O base */
#define ATA_DATA        0x00
#define ATA_ERROR       0x01
#define ATA_FEATURES    0x01
#define ATA_SECCOUNT    0x02
#define ATA_LBA_LO      0x03
#define ATA_LBA_MID     0x04
#define ATA_LBA_HI      0x05
#define ATA_DRIVE_SEL   0x06
#define ATA_STATUS      0x07
#define ATA_COMMAND     0x07
#define ATA_ALT_STATUS  0x206   /* offset from control base */

/* ATA status bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

/* ATA commands */
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7

/* Drive select */
#define ATA_MASTER  0xA0
#define ATA_SLAVE   0xB0

/* QEMU default ATA I/O ports */
#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6
#define ATA_SECONDARY_IO  0x170

#define SECTOR_SIZE  512
#define DISK_MAX_DRIVES 4

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  drive_sel;   /* ATA_MASTER or ATA_SLAVE */
    int      present;
    uint32_t sectors;     /* usable LBA28 sectors for current PIO driver */
    uint64_t sectors_total; /* IDENTIFY capacity (LBA48 when available) */
    uint8_t  lba48;
    char     model[41];
    char     serial[21];
    char     firmware[9];
} disk_drive_t;

extern disk_drive_t disk_drives[DISK_MAX_DRIVES];
extern int          disk_count;
extern volatile uint32_t disk_last_status;
extern volatile uint32_t disk_last_error_reg;
extern volatile uint32_t disk_last_error_lba;
extern volatile uint32_t disk_read_ops;
extern volatile uint32_t disk_write_ops;
extern volatile uint32_t disk_io_errors;
extern volatile uint32_t disk_last_data_lba;
extern volatile uint32_t disk_last_data_word;

/* Initialize: detect all ATA drives */
void disk_init(void);

/* Read/write single sector (LBA28) */
int disk_read_sector (int drive, uint32_t lba, uint8_t *buf);
int disk_write_sector(int drive, uint32_t lba, const uint8_t *buf);

/* Read/write multiple sectors */
int disk_read (int drive, uint32_t lba, uint32_t count, uint8_t *buf);
int disk_write(int drive, uint32_t lba, uint32_t count, const uint8_t *buf);

void disk_print_info(void);
uint32_t disk_capacity_mib(int drive); /* saturated human-readable capacity */

#endif
