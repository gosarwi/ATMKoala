/* disk.c — ATA PIO disk driver for atmkoala OS v0.5
 *
 *  Detects up to 4 ATA drives (primary/secondary × master/slave).
 *  Uses LBA28 PIO mode — no DMA, but universal compatibility with QEMU.
 *  QEMU default: -hda disk.img creates a primary-master ATA drive.
 */
#include "disk.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

disk_drive_t disk_drives[DISK_MAX_DRIVES];
int          disk_count = 0;
/* Exported diagnostic counters for disk I/O validation. */
volatile uint32_t disk_read_ops = 0;
volatile uint32_t disk_write_ops = 0;
volatile uint32_t disk_io_errors = 0;
volatile uint32_t disk_last_data_lba = 0;
volatile uint32_t disk_last_data_word = 0;
volatile uint32_t disk_last_status = 0;
volatile uint32_t disk_last_error_reg = 0;
volatile uint32_t disk_last_error_lba = 0;

static void ata_400ns_delay(uint16_t io) {
    inb(io + ATA_ALT_STATUS); inb(io + ATA_ALT_STATUS);
    inb(io + ATA_ALT_STATUS); inb(io + ATA_ALT_STATUS);
}

/* ATA IDENTIFY stores printable text with the bytes of each 16-bit word
 * swapped. Normalize it once at detection time for every UI consumer. */
static void ata_identify_text(const uint16_t *id,int first,int words,char *out){
    int n=0;
    for(int i=0;i<words;i++){out[n++]=(char)(id[first+i]>>8);out[n++]=(char)(id[first+i]&0xFF);}
    out[n]=0;
    for(int i=n-1;i>=0&&(out[i]==' '||out[i]==0);i--)out[i]=0;
    for(int i=0;out[i];i++)if((uint8_t)out[i]<0x20||(uint8_t)out[i]>0x7E)out[i]='?';
}

static int ata_wait_ready(uint16_t io, int timeout_k) {
    while (timeout_k-- > 0) {
        uint8_t st = inb(io + ATA_STATUS);
        disk_last_status = st;
        if (st & ATA_SR_ERR)  { disk_last_error_reg=inb(io+ATA_ERROR); return -1; }
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRDY)) return 0;
    }
    return -2; /* timeout */
}

static int ata_wait_drq(uint16_t io, int timeout_k) {
    while (timeout_k-- > 0) {
        uint8_t st = inb(io + ATA_STATUS);
        disk_last_status = st;
        if (st & ATA_SR_ERR) { disk_last_error_reg=inb(io+ATA_ERROR); return -1; }
        if (st & ATA_SR_DRQ) return 0;
    }
    return -2;
}

/* Before issuing a new command, only BSY is meaningful.  ERR may belong
 * to the device selected by an earlier probe and is cleared by the command. */
static int ata_wait_not_busy(uint16_t io, int timeout_k) {
    while (timeout_k-- > 0) {
        uint8_t st=inb(io + ATA_STATUS); disk_last_status=st;
        if (!(st & ATA_SR_BSY)) return 0;
    }
    return -2;
}

/* Send IDENTIFY and populate drive info */
static int ata_identify(disk_drive_t *drv) {
    uint16_t io = drv->io_base;

    /* Select drive */
    outb(io + ATA_DRIVE_SEL, drv->drive_sel);
    ata_400ns_delay(io);

    /* Zero sector count / LBA regs */
    outb(io + ATA_SECCOUNT, 0);
    outb(io + ATA_LBA_LO,   0);
    outb(io + ATA_LBA_MID,  0);
    outb(io + ATA_LBA_HI,   0);

    /* Send IDENTIFY */
    outb(io + ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay(io);

    uint8_t st = inb(io + ATA_STATUS);
    if (st == 0x00 || st == 0xFF) return -1; /* no drive */

    /* Wait for BSY clear */
    int timeout = 100000;
    while ((inb(io + ATA_STATUS) & ATA_SR_BSY) && --timeout);
    if (!timeout) return -1;

    /* Check if ATA (not ATAPI) */
    if (inb(io + ATA_LBA_MID) != 0 || inb(io + ATA_LBA_HI) != 0)
        return -1; /* ATAPI — skip */

    if (ata_wait_drq(io, 100000) < 0) return -1;

    /* Read 256 words of IDENTIFY data */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) identify[i] = inw(io + ATA_DATA);

    /* Word 60-61: LBA28 sector count. Words 100-103 advertise the
     * larger LBA48 capacity; current PIO commands still use LBA28. */
    drv->sectors = ((uint32_t)identify[61] << 16) | identify[60];
    uint64_t lba48=(uint64_t)identify[100]|((uint64_t)identify[101]<<16)|
                   ((uint64_t)identify[102]<<32)|((uint64_t)identify[103]<<48);
    drv->lba48=((identify[83]&(1u<<10))&&lba48)?1:0;
    drv->sectors_total=drv->lba48?lba48:(uint64_t)drv->sectors;
    ata_identify_text(identify,10,10,drv->serial);
    ata_identify_text(identify,23,4,drv->firmware);
    ata_identify_text(identify,27,20,drv->model);

    drv->present = 1;
    return 0;
}

/* ── Public API ─────────────────────────────────────────────── */
void disk_init(void) {
    disk_count = 0;
    disk_read_ops=disk_write_ops=disk_io_errors=0;
    disk_last_status=disk_last_error_reg=disk_last_error_lba=0;
    disk_last_data_lba=disk_last_data_word=0;
    kmemset(disk_drives, 0, sizeof(disk_drives));

    struct { uint16_t io; uint16_t ctrl; uint8_t sel; } probes[] = {
        {ATA_PRIMARY_IO,   ATA_PRIMARY_CTRL, ATA_MASTER},
        {ATA_PRIMARY_IO,   ATA_PRIMARY_CTRL, ATA_SLAVE },
        {ATA_SECONDARY_IO, 0x376,            ATA_MASTER},
        {ATA_SECONDARY_IO, 0x376,            ATA_SLAVE },
    };

    for (int i = 0; i < DISK_MAX_DRIVES; i++) {
        disk_drives[i].io_base   = probes[i].io;
        disk_drives[i].ctrl_base = probes[i].ctrl;
        disk_drives[i].drive_sel = probes[i].sel;
        if (ata_identify(&disk_drives[i]) == 0) {
            disk_count++;
        }
    }
}

int disk_read_sector(int drv_idx, uint32_t lba, uint8_t *buf) {
    if (drv_idx < 0 || drv_idx >= DISK_MAX_DRIVES) return -1;
    disk_drive_t *drv = &disk_drives[drv_idx];
    if (!drv->present) return -1;

    uint16_t io = drv->io_base;

    /* Select the target before polling; probing another ATA position may
     * leave ERR set in the controller status register. */
    outb(io + ATA_DRIVE_SEL, (uint8_t)(drv->drive_sel | 0x40 |
                              ((lba >> 24) & 0x0F)));
    ata_400ns_delay(io);
    if (ata_wait_not_busy(io, 100000) < 0) { disk_io_errors++; return -1; }

    /* LBA28 setup */
    outb(io + ATA_SECCOUNT, 1);
    outb(io + ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(io + ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));
    outb(io + ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(io + ATA_COMMAND, ATA_CMD_READ_PIO);
    ata_400ns_delay(io);

    if (ata_wait_drq(io, 200000) < 0) { disk_io_errors++; return -1; }

    /* Read 256 words */
    uint16_t *w = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) w[i] = inw(io + ATA_DATA);
    disk_read_ops++;
    return 0;
}

static int disk_flush(disk_drive_t *drv) {
    uint16_t io=drv->io_base;
    outb(io + ATA_COMMAND, ATA_CMD_FLUSH);
    ata_400ns_delay(io);
    if(ata_wait_ready(io,20000000)<0){disk_io_errors++;return -1;}
    return 0;
}

static int disk_write_sector_impl(int drv_idx, uint32_t lba, const uint8_t *buf, int do_flush) {
    disk_last_error_lba = lba;
    if (drv_idx < 0 || drv_idx >= DISK_MAX_DRIVES) return -1;
    disk_drive_t *drv = &disk_drives[drv_idx];
    if (!drv->present) return -1;

    uint16_t io = drv->io_base;
    if (lba >= 90u) {
        disk_last_data_lba = lba;
        disk_last_data_word = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
    }

    /* Select the requested target before polling; controller ERR can reflect
     * an earlier probe of another device. */
    outb(io + ATA_DRIVE_SEL, (uint8_t)(drv->drive_sel | 0x40 |
                              ((lba >> 24) & 0x0F)));
    ata_400ns_delay(io);
    if (ata_wait_not_busy(io, 100000) < 0) { disk_io_errors++; return -1; }
    outb(io + ATA_SECCOUNT, 1);
    outb(io + ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(io + ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));
    outb(io + ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(io + ATA_COMMAND, ATA_CMD_WRITE_PIO);
    ata_400ns_delay(io);

    if (ata_wait_drq(io, 200000) < 0) { disk_io_errors++; return -1; }

    /* Write 256 words, then wait until the data command is complete before
     * submitting FLUSH.  Issuing FLUSH while DRQ is still active can discard
     * the just-transferred sector on ATA PIO devices. */
    const uint16_t *w = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++) outw(io + ATA_DATA, w[i]);
    ata_400ns_delay(io);
    if (ata_wait_ready(io, 20000000) < 0) { disk_io_errors++; return -1; }

    if(do_flush && disk_flush(drv)<0) return -1;
    disk_write_ops++;
    return 0;
}

int disk_write_sector(int drv_idx, uint32_t lba, const uint8_t *buf) {
    return disk_write_sector_impl(drv_idx,lba,buf,1);
}

int disk_read(int drv, uint32_t lba, uint32_t count, uint8_t *buf) {
    for (uint32_t i = 0; i < count; i++) {
        if (disk_read_sector(drv, lba + i, buf + i * SECTOR_SIZE) < 0)
            return -1;
    }
    return 0;
}

int disk_write(int drv, uint32_t lba, uint32_t count, const uint8_t *buf) {
    if(drv<0||drv>=DISK_MAX_DRIVES||!disk_drives[drv].present||!count)return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (disk_write_sector_impl(drv, lba + i, buf + i * SECTOR_SIZE, 0) < 0)
            return -1;
    }
    return disk_flush(&disk_drives[drv]);
}

uint32_t disk_capacity_mib(int drive){
    if(drive<0||drive>=DISK_MAX_DRIVES||!disk_drives[drive].present)return 0;
    uint64_t mib=disk_drives[drive].sectors_total/2048u;
    return mib>0xFFFFFFFFu?0xFFFFFFFFu:(uint32_t)mib;
}

void disk_print_info(void) {
    char buf[16];
    if (disk_count == 0) {
        terminal_writeln("  No ATA drives detected");
        terminal_writeln("  (Use QEMU: -hda disk.img to add a drive)");
        return;
    }
    for (int i = 0; i < DISK_MAX_DRIVES; i++) {
        disk_drive_t *d = &disk_drives[i];
        if (!d->present) continue;
        terminal_write("  hd");
        terminal_putchar('a' + i);
        terminal_write(": ");
        terminal_write(d->model[0] ? d->model : "ATA Disk");
        terminal_write("  ");
        kuitoa(disk_capacity_mib(i), buf, 10);
        terminal_write(buf);
        terminal_writeln(" MB");
    }
}
