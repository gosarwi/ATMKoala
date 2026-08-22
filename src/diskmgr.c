/*  diskmgr.c — Disk partitioning TUI for atmkoala
 *
 *  Full-screen VGA text-mode partition manager. Reads/writes a real
 *  MBR partition table (partmgr.c) and can format a newly-created
 *  partition with CatFS. Navigation: arrow keys, Enter, Esc.
 */

#include "diskmgr.h"
#include "partmgr.h"
#include "catfs.h"
#include "disk.h"
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "util.h"
#include "ossdk.h"
#include <stdint.h>
#include <stddef.h>

/* ── Layout (matches untui.c for visual consistency) ─────────── */
#define TUI_W   80
#define TUI_CONTENT_TOP  3
#define TUI_HINT_ROW     23
#define TUI_FOOTER_ROW   24

#define C_TITLE_FG   VGA_BLACK
#define C_TITLE_BG   VGA_LIGHT_CYAN
#define C_STATUS_FG  VGA_WHITE
#define C_STATUS_BG  VGA_BLUE
#define C_NORMAL_FG  VGA_LIGHT_GREY
#define C_NORMAL_BG  VGA_BLACK
#define C_SELECT_FG  VGA_BLACK
#define C_SELECT_BG  VGA_LIGHT_CYAN
#define C_HINT_FG    VGA_DARK_GREY
#define C_HINT_BG    VGA_BLACK
#define C_OK_FG      VGA_LIGHT_GREEN
#define C_ERR_FG     VGA_LIGHT_RED
#define C_WARN_FG    VGA_YELLOW
#define C_LABEL_FG   VGA_YELLOW
#define C_FRAME_FG   VGA_CYAN
#define C_INPUT_FG   VGA_WHITE
#define C_INPUT_BG   VGA_DARK_GREY

/* ── Drawing primitives (same idiom as untui.c) ──────────────── */
static void tui_setcol(uint8_t fg, uint8_t bg) { terminal_set_color(fg, bg); }
static void tui_goto(int row, int col)         { terminal_set_cursor(row, col); }
static void tui_putch(char c)                  { terminal_putchar(c); }
static void tui_write(const char *s)           { terminal_write(s); }

static void tui_fill_row(int row, char c, uint8_t fg, uint8_t bg) {
    tui_setcol(fg, bg);
    tui_goto(row, 0);
    for (int i = 0; i < TUI_W; i++) tui_putch(c);
}

static void tui_write_field(int row, int col, const char *s, int width,
                              uint8_t fg, uint8_t bg) {
    tui_setcol(fg, bg);
    tui_goto(row, col);
    int len = (int)kstrlen(s);
    for (int i = 0; i < width; i++)
        tui_putch((i < len) ? s[i] : ' ');
}

static void tui_clear_content(void) {
    tui_setcol(C_NORMAL_FG, C_NORMAL_BG);
    for (int r = TUI_CONTENT_TOP; r <= TUI_HINT_ROW - 1; r++) {
        tui_goto(r, 0);
        for (int c = 0; c < TUI_W; c++) tui_putch(' ');
    }
}

static void tui_write_centered(int row, const char *s, uint8_t fg, uint8_t bg) {
    int len = (int)kstrlen(s);
    int col = (TUI_W - len) / 2;
    if (col < 0) col = 0;
    tui_setcol(fg, bg);
    tui_goto(row, 0);
    for (int i = 0; i < col; i++) tui_putch(' ');
    tui_write(s);
    for (int i = col + len; i < TUI_W; i++) tui_putch(' ');
}

static void tui_draw_chrome(const char *screen_name, int drive) {
    tui_fill_row(0, ' ', C_TITLE_FG, C_TITLE_BG);
    tui_goto(0, 0);
    tui_setcol(C_TITLE_FG, C_TITLE_BG);
    char buf[80];
    ksnprintf(buf, sizeof(buf), " atmkoala  Disk Manager  |  %s", screen_name);
    tui_write(buf);
    tui_goto(0, TUI_W - 10);
    tui_write("  v1.0    ");

    tui_fill_row(1, ' ', C_STATUS_FG, C_STATUS_BG);
    tui_goto(1, 1);
    tui_setcol(C_STATUS_FG, C_STATUS_BG);
    if (drive >= 0 && disk_drives[drive].present) {
        ksnprintf(buf, sizeof(buf), "Drive %d: %s  |  %u MB  |  %u sectors",
            drive, disk_drives[drive].model,
            disk_drives[drive].sectors / 2048,
            disk_drives[drive].sectors);
        tui_write(buf);
    } else {
        tui_write("No drive selected");
    }

    tui_setcol(C_FRAME_FG, C_NORMAL_BG);
    tui_goto(2, 0);
    for (int i = 0; i < TUI_W; i++) tui_putch('-');

    tui_fill_row(TUI_HINT_ROW, ' ', C_HINT_FG, C_HINT_BG);
    tui_goto(TUI_HINT_ROW, 0);
    tui_setcol(C_HINT_FG, C_HINT_BG);
    tui_write(" Arrows=select  Enter=action  Esc=back  Q=quit");

    tui_fill_row(TUI_FOOTER_ROW, ' ', C_HINT_FG, VGA_BLUE);
    tui_goto(TUI_FOOTER_ROW, 0);
    tui_setcol(VGA_WHITE, VGA_BLUE);
    tui_write(" atmkoala Disk Manager  |  MBR partitions  |  CatFS format");
}

static int tui_confirm(int row, const char *msg) {
    tui_setcol(C_WARN_FG, C_NORMAL_BG);
    tui_goto(row, 2);
    tui_write(msg);
    tui_write(" [y/N]: ");
    int k = keyboard_getkey();
    return (k == 'y' || k == 'Y');
}
static int tui_confirm_word(int row,const char *msg,const char *word){
    char in[16];int len=0;in[0]=0;tui_setcol(C_WARN_FG,C_NORMAL_BG);tui_goto(row,2);tui_write(msg);tui_write(" Type ");tui_write(word);tui_write(": ");
    while(1){int k=keyboard_getkey();if(k=='\n'||k=='\r')break;if(k==KEY_ESC)return 0;if((k=='\b'||k==127)&&len>0){in[--len]=0;continue;}if(((k>='a'&&k<='z')||(k>='A'&&k<='Z'))&&len<(int)sizeof(in)-1){if(k>='a'&&k<='z')k-=32;in[len++]=(char)k;in[len]=0;}}
    return !kstrcmp(in,word);
}

static void tui_readline(int row, int col, int width, char *out, int maxlen) {
    int len = 0, pos = 0;
    out[0] = 0;
    while (1) {
        tui_setcol(C_INPUT_FG, C_INPUT_BG);
        tui_goto(row, col);
        for (int i = 0; i < width; i++)
            tui_putch((i < len) ? out[i] : ' ');
        tui_goto(row, col + pos);

        int k = keyboard_getkey();
        if (k == '\n' || k == '\r') break;
        if (k == KEY_ESC) { out[0] = 0; break; }
        if ((k == '\b' || k == 127) && pos > 0) {
            for (int i = pos - 1; i < len - 1; i++) out[i] = out[i+1];
            pos--; len--; out[len] = 0;
        } else if (k == KEY_LEFT  && pos > 0) pos--;
        else if (k == KEY_RIGHT && pos < len) pos++;
        else if (k >= '0' && k <= '9' && len < maxlen - 1) {
            for (int i = len; i > pos; i--) out[i] = out[i-1];
            out[pos++] = (char)k;
            len++; out[len] = 0;
        }
    }
}

/* ── Screens ──────────────────────────────────────────────────── */
typedef enum {
    SCREEN_SELECT_DRIVE = 0,
    SCREEN_PARTITIONS,
    SCREEN_ADD,
    SCREEN_QUIT,
} dm_screen_t;

static int      g_drive = -1;
static mbr_table_t g_mbr;
static int      g_mbr_loaded = 0;
static int      g_dirty = 0; /* staged changes only; disk is touched by WRITE confirmation. */

static void ensure_mbr_loaded(void) {
    if (g_drive < 0) return;
    if (mbr_read(g_drive, &g_mbr) < 0) {
        /* No valid MBR yet on this disk — stage an empty table only.
         * Nothing is written until explicit WRITE confirmation. */
        mbr_init_empty(&g_mbr);
    }
    g_mbr_loaded = 1; g_dirty = 0;
}
static uint8_t dm_type_from_key(int k){if(k=='1')return PART_TYPE_CATFS;if(k=='2')return PART_TYPE_LINUX;if(k=='3')return PART_TYPE_LINUX_SWAP;if(k=='4')return PART_TYPE_FAT32;return PART_TYPE_EMPTY;}
static void dm_set_bootable(mbr_table_t *t,int idx){if(!t||idx<0||idx>=PART_MAX_ENTRIES||t->entries[idx].type==PART_TYPE_EMPTY)return;for(int i=0;i<PART_MAX_ENTRIES;i++)t->entries[i].status=0;t->entries[idx].status=0x80;}
int diskmgr_selftest(void){mbr_table_t t;mbr_init_empty(&t);if(mbr_add_partition(&t,PART_TYPE_CATFS,2048,4096,0)!=0)return -1;if(mbr_add_partition(&t,PART_TYPE_LINUX,8192,4096,0)!=1)return -1;dm_set_bootable(&t,1);if(t.entries[0].status||t.entries[1].status!=0x80||mbr_validate(&t)<0)return -1;t.entries[0].type=PART_TYPE_LINUX_SWAP;return t.entries[0].type==PART_TYPE_LINUX_SWAP&&mbr_validate(&t)==0?0:-1;}

/* ── SCREEN: select drive ────────────────────────────────────── */
static dm_screen_t screen_select_drive(void) {
    int sel = 0;
    int present_idx[DISK_MAX_DRIVES];
    int n = 0;
    for (int i = 0; i < DISK_MAX_DRIVES; i++)
        if (disk_drives[i].present) present_idx[n++] = i;

    while (1) {
        terminal_clear();
        tui_draw_chrome("Select Drive", -1);
        tui_clear_content();

        tui_write_centered(TUI_CONTENT_TOP + 1,
            "Select a drive to partition", C_LABEL_FG, C_NORMAL_BG);

        if (n == 0) {
            tui_write_centered(TUI_CONTENT_TOP + 4,
                "No ATA drives detected.", C_ERR_FG, C_NORMAL_BG);
            tui_setcol(C_HINT_FG, C_NORMAL_BG);
            tui_goto(TUI_CONTENT_TOP + 6, 2);
            tui_write("Press any key to exit...");
            keyboard_getkey();
            return SCREEN_QUIT;
        }

        for (int i = 0; i < n; i++) {
            int d = present_idx[i];
            char line[80];
            ksnprintf(line, sizeof(line), "  Drive %d:  %-20s  %5u MB",
                d, disk_drives[d].model, disk_drives[d].sectors / 2048);
            int row = TUI_CONTENT_TOP + 3 + i;
            if (i == sel)
                tui_write_field(row, 2, line, TUI_W-4, C_SELECT_FG, C_SELECT_BG);
            else
                tui_write_field(row, 2, line, TUI_W-4, C_NORMAL_FG, C_NORMAL_BG);
        }

        int k = keyboard_getkey();
        if (k == KEY_UP)   { if (sel > 0) sel--; }
        if (k == KEY_DOWN) { if (sel < n-1) sel++; }
        if (k == 'q' || k == 'Q' || k == KEY_ESC) return SCREEN_QUIT;
        if (k == '\n' || k == '\r') {
            g_drive = present_idx[sel];
            ensure_mbr_loaded();
            sdk_serial_write("[cfdisk] ready\n");
            return SCREEN_PARTITIONS;
        }
    }
}

/* ── SCREEN: partition table view ────────────────────────────── */
static dm_screen_t screen_partitions(void) {
    int sel = 0;

    while (1) {
        terminal_clear();
        tui_draw_chrome("Partitions", g_drive);
        tui_clear_content();

        tui_write_centered(TUI_CONTENT_TOP + 1,
            g_dirty ? "STAGED MBR — N add D delete T type B boot R reload W WRITE" : "MBR table — N add D delete T type B boot R reload W write",
            g_dirty ? C_WARN_FG : C_LABEL_FG, C_NORMAL_BG);

        tui_setcol(VGA_CYAN, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 3, 2);
        tui_write("  #  Boot  Type    Start LBA   Sectors    Size");

        tui_setcol(C_FRAME_FG, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 4, 2);
        for (int i = 0; i < TUI_W-4; i++) tui_putch('-');

        for (int i = 0; i < PART_MAX_ENTRIES; i++) {
            mbr_entry_t *e = &g_mbr.entries[i];
            char line[80];
            if (e->type == PART_TYPE_EMPTY) {
                ksnprintf(line, sizeof(line), "  %d  %-4s  %-8s  %-10s  %-9s  %s",
                    i, "", "(empty)", "-", "-", "-");
            } else {
                uint32_t mb = e->sector_count / 2048;
                ksnprintf(line, sizeof(line), "  %d  %-4s  %-8s  %-10u  %-9u  %u MB",
                    i, (e->status & 0x80) ? "yes" : "no",
                    part_type_name(e->type), e->lba_start, e->sector_count, mb);
            }
            int row = TUI_CONTENT_TOP + 5 + i;
            uint8_t fg = (e->type == PART_TYPE_EMPTY) ? C_HINT_FG : C_NORMAL_FG;
            if (i == sel) {
                tui_write_field(row, 2, line, TUI_W-4, C_SELECT_FG, C_SELECT_BG);
            } else {
                tui_write_field(row, 2, line, TUI_W-4, fg, C_NORMAL_BG);
            }
        }

        /* Free space summary */
        uint32_t total = disk_drives[g_drive].sectors;
        uint32_t used = 0;
        for (int i = 0; i < PART_MAX_ENTRIES; i++)
            if (g_mbr.entries[i].type != PART_TYPE_EMPTY)
                used += g_mbr.entries[i].sector_count;
        char sumline[80];
        ksnprintf(sumline, sizeof(sumline),
            "  Total: %u MB   Used: %u MB   Free: %u MB",
            total / 2048, used / 2048, (total > used) ? (total-used)/2048 : 0);
        tui_setcol(C_LABEL_FG, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 10, 2);
        tui_write(sumline);
        tui_setcol(g_dirty ? C_WARN_FG : C_HINT_FG, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 12, 2);
        tui_write(g_dirty ? "STAGED ONLY: disk changes only after W then WRITE." : "Clean table loaded from sector 0; edits are staged in memory.");

        int k = keyboard_getkey();
        if (k == KEY_UP)   { if (sel > 0) sel--; continue; }
        if (k == KEY_DOWN) { if (sel < PART_MAX_ENTRIES-1) sel++; continue; }
        if (k == 'q' || k == 'Q' || k == KEY_ESC) { if(!g_dirty || tui_confirm_word(TUI_HINT_ROW-1,"Discard all staged changes?","DISCARD")) return SCREEN_SELECT_DRIVE; continue; }
        if (k == 'n' || k == 'N') return SCREEN_ADD;
        if ((k == 'd' || k == 'D') && g_mbr.entries[sel].type != PART_TYPE_EMPTY) { if(tui_confirm(TUI_HINT_ROW-1,"Stage removal of selected entry? Data sectors are not erased")){mbr_remove_partition(&g_mbr,sel);g_dirty=1;} continue; }
        if ((k == 't' || k == 'T') && g_mbr.entries[sel].type != PART_TYPE_EMPTY) { tui_setcol(C_LABEL_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("Type: 1=CatFS  2=Linux  3=swap  4=FAT32  Esc cancel");uint8_t type=dm_type_from_key(keyboard_getkey());if(type){g_mbr.entries[sel].type=type;g_dirty=1;}continue; }
        if ((k == 'b' || k == 'B') && g_mbr.entries[sel].type != PART_TYPE_EMPTY) { dm_set_bootable(&g_mbr,sel);g_dirty=1;continue; }
        if (k == 'r' || k == 'R') { if(!g_dirty || tui_confirm_word(TUI_HINT_ROW-1,"Discard staged table and re-read sector 0?","DISCARD"))ensure_mbr_loaded();continue; }
        if ((k == 'f' || k == 'F') && g_mbr.entries[sel].type == PART_TYPE_CATFS) { if(g_dirty){tui_setcol(C_WARN_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("Write or reload staged table before formatting CatFS.");keyboard_getkey();continue;}if(tui_confirm_word(TUI_HINT_ROW-1,"Format selected CatFS partition? ALL DATA LOST.","FORMAT")){tui_clear_content();tui_write_centered(TUI_CONTENT_TOP+5,"Formatting...",C_WARN_FG,C_NORMAL_BG);int r=catfs_format_at(g_drive,g_mbr.entries[sel].lba_start,"catfs");tui_setcol(r==0?C_OK_FG:C_ERR_FG,C_NORMAL_BG);tui_goto(TUI_CONTENT_TOP+7,2);tui_write(r==0?"Format complete.":"Format failed.");tui_goto(TUI_CONTENT_TOP+9,2);tui_write("Press any key...");keyboard_getkey();}continue; }
        if (k == 'w' || k == 'W') { if(!g_dirty){tui_setcol(C_HINT_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("No staged MBR change to write.");pit_sleep(80);continue;}if(mbr_validate_drive(g_drive,&g_mbr)<0){tui_setcol(C_ERR_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("Refused: staged MBR failed overlap/range validation.");keyboard_getkey();continue;}if(tui_confirm_word(TUI_HINT_ROW-1,"This changes only MBR sector 0.","WRITE")){sdk_serial_write("[cfdisk] write-accepted\n");int r=mbr_write(g_drive,&g_mbr,0);mbr_table_t verify;if(r==0&&mbr_read(g_drive,&verify)==0&&kmemcmp(verify.entries,g_mbr.entries,sizeof(g_mbr.entries))==0){g_dirty=0;sdk_serial_write("[cfdisk] write-ok\n");tui_setcol(C_OK_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("MBR written and re-read successfully.");}else{tui_setcol(C_ERR_FG,C_NORMAL_BG);tui_goto(TUI_HINT_ROW-1,2);tui_write("WRITE failed or verify mismatch; staged table retained.");}keyboard_getkey();}continue; }
    }
}

/* ── SCREEN: add partition wizard ────────────────────────────── */
static dm_screen_t screen_add(void) {
    terminal_clear();
    tui_draw_chrome("New Partition", g_drive);
    tui_clear_content();

    uint32_t total = disk_drives[g_drive].sectors;

    /* Find first free LBA after the last used partition (simple
     * "append at the end" allocator — avoids overlap by construction;
     * the user can still type a custom start if they want a gap). */
    uint32_t suggested_start = 2048; /* leave room for the MBR + alignment */
    for (int i = 0; i < PART_MAX_ENTRIES; i++) {
        mbr_entry_t *e = &g_mbr.entries[i];
        if (e->type == PART_TYPE_EMPTY) continue;
        uint32_t end = e->lba_start + e->sector_count;
        if (end > suggested_start) suggested_start = end;
    }

    char start_buf[16], size_buf[16];
    ksnprintf(start_buf, sizeof(start_buf), "%u", suggested_start);
    ksnprintf(size_buf,  sizeof(size_buf),  "%u", (total > suggested_start)
        ? (total - suggested_start) : 0);

    tui_write_centered(TUI_CONTENT_TOP + 1,
        "Create a new CatFS partition", C_LABEL_FG, C_NORMAL_BG);

    int row = TUI_CONTENT_TOP + 3;
    tui_setcol(C_LABEL_FG, C_NORMAL_BG);
    tui_goto(row, 4); tui_write("Start LBA   : ");
    tui_readline(row, 19, 12, start_buf, sizeof(start_buf));
    row += 2;

    tui_setcol(C_LABEL_FG, C_NORMAL_BG);
    tui_goto(row, 4); tui_write("Size (MB)   : ");
    char mb_buf[16];
    uint32_t suggested_mb = (total > suggested_start) ? (total - suggested_start) / 2048 : 0;
    ksnprintf(mb_buf, sizeof(mb_buf), "%u", suggested_mb);
    tui_readline(row, 19, 12, mb_buf, sizeof(mb_buf));
    row += 2;

    int bootable = 0;
    tui_setcol(C_HINT_FG, C_NORMAL_BG);
    tui_goto(row, 4);
    tui_write("Make bootable? (y/N): ");
    int k = keyboard_getkey();
    bootable = (k == 'y' || k == 'Y');

    uint32_t start = (uint32_t)kstrtoi(start_buf);
    uint32_t sectors = (uint32_t)kstrtoi(mb_buf) * 2048;

    row += 2;
    if (start == 0 || sectors == 0) {
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_goto(row, 4); tui_write("Invalid start or size.");
    } else if (mbr_overlaps(&g_mbr, start, sectors, -1)) {
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_goto(row, 4); tui_write("Overlaps an existing partition.");
    } else if (start >= total || sectors > total - start) {
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_goto(row, 4); tui_write("Exceeds drive size.");
    } else {
        int idx = mbr_add_partition(&g_mbr, PART_TYPE_CATFS, start, sectors, bootable);
        tui_setcol(idx >= 0 ? C_OK_FG : C_ERR_FG, C_NORMAL_BG);
        tui_goto(row, 4);
        tui_write(idx >= 0 ? "Partition staged — press W then type WRITE to commit only the MBR."
                            : "No free partition slot.");
        if(idx>=0) { g_dirty=1; sdk_serial_write("[cfdisk] staged-add\n"); }
    }

    row += 2;
    tui_setcol(C_HINT_FG, C_NORMAL_BG);
    tui_goto(row, 4); tui_write("Press any key...");
    keyboard_getkey();
    return SCREEN_PARTITIONS;
}

/* ── Entry point ──────────────────────────────────────────────── */
void diskmgr_run(void) {
    dm_screen_t screen = SCREEN_SELECT_DRIVE;
    g_drive = -1;
    g_mbr_loaded = 0;
    g_dirty = 0;

    while (screen != SCREEN_QUIT) {
        switch (screen) {
            case SCREEN_SELECT_DRIVE: screen = screen_select_drive(); break;
            case SCREEN_PARTITIONS:   screen = screen_partitions();   break;
            case SCREEN_ADD:          screen = screen_add();          break;
            default:                  screen = SCREEN_QUIT;          break;
        }
    }

    terminal_clear();
    terminal_print_logo();
}
