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

static void ensure_mbr_loaded(void) {
    if (g_drive < 0) return;
    if (mbr_read(g_drive, &g_mbr) < 0) {
        /* No valid MBR yet on this disk — start from an empty table.
         * Nothing is written until the user explicitly saves. */
        mbr_init_empty(&g_mbr);
    }
    g_mbr_loaded = 1;
}

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
            "MBR Partition Table  (N=new  D=delete  F=format CatFS  W=write to disk)",
            C_LABEL_FG, C_NORMAL_BG);

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

        int k = keyboard_getkey();
        if (k == KEY_UP)   { if (sel > 0) sel--; continue; }
        if (k == KEY_DOWN) { if (sel < PART_MAX_ENTRIES-1) sel++; continue; }
        if (k == 'q' || k == 'Q' || k == KEY_ESC) return SCREEN_SELECT_DRIVE;

        if (k == 'n' || k == 'N') return SCREEN_ADD;

        if ((k == 'd' || k == 'D') &&
            g_mbr.entries[sel].type != PART_TYPE_EMPTY) {
            if (tui_confirm(TUI_HINT_ROW - 1,
                "Delete this partition entry? (data is not erased, only the table entry)")) {
                mbr_remove_partition(&g_mbr, sel);
            }
            continue;
        }

        if ((k == 'f' || k == 'F') &&
            g_mbr.entries[sel].type == PART_TYPE_CATFS) {
            if (tui_confirm(TUI_HINT_ROW - 1,
                "Format this partition with CatFS? ALL DATA ON IT WILL BE LOST")) {
                tui_clear_content();
                tui_write_centered(TUI_CONTENT_TOP + 5,
                    "Formatting...", C_WARN_FG, C_NORMAL_BG);
                int r = catfs_format_at(g_drive, g_mbr.entries[sel].lba_start, "catfs");
                tui_setcol(r == 0 ? C_OK_FG : C_ERR_FG, C_NORMAL_BG);
                tui_goto(TUI_CONTENT_TOP + 7, 2);
                tui_write(r == 0 ? "Format complete." : "Format failed.");
                tui_setcol(C_HINT_FG, C_NORMAL_BG);
                tui_goto(TUI_CONTENT_TOP + 9, 2);
                tui_write("Press any key...");
                keyboard_getkey();
            }
            continue;
        }

        if (k == 'w' || k == 'W') {
            if (tui_confirm(TUI_HINT_ROW - 1,
                "Write this partition table to disk now?")) {
                int r = mbr_write(g_drive, &g_mbr, 0);
                tui_setcol(r == 0 ? C_OK_FG : C_ERR_FG, C_NORMAL_BG);
                tui_goto(TUI_HINT_ROW - 1, 2);
                tui_write(r == 0 ? "Partition table written.        " : "Write FAILED.                   ");
                pit_sleep(100);
            }
            continue;
        }
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
        tui_write(idx >= 0 ? "Partition added (not yet written to disk — press W on the previous screen)."
                            : "No free partition slot.");
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
