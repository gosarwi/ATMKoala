/* store.c — Exp Store
 * Built-in VFS package catalogue using .tar.zst archives.
 */
#include "store.h"
#include "vga.h"
#include "vfs.h"
#include "keyboard.h"
#include "util.h"
#include "config.h"
#include "tarzst.h"
#include <stdint.h>

typedef struct {
    const char *id;
    const char *name;
    const char *version;
    const char *category;
    const char *desc;
    const char *size;
    int         installed;
} store_pkg_t;

static store_pkg_t catalog[] = {
    {"snake",      "Snake Classic",      "1.0", "Games",   "Классическая змейка (встроена)",       "built-in", 1},
    {"tetris",     "Tetris",             "1.0", "Games",   "Тетрис (встроен)",                     "built-in", 1},
    {"pong",       "Pong",               "1.0", "Games",   "Классический Pong (встроен)",           "built-in", 1},
    {"breakout",   "Breakout",           "1.0", "Games",   "Разбивание кирпичей (встроен)",         "built-in", 1},
    {"caramelide", "CaramelIDE",         "0.1", "Dev",     "Простой текстовый редактор",            "built-in", 1},
    {"sysmon",     "System Monitor",     "1.0", "System",  "Монитор системы в DE",                  "built-in", 1},
    {"caramelnet", "QewoxNet",         "0.5", "Network", "Сетевой стек RTL8168/8139",             "built-in", 1},
    {"fat32",      "FAT32 Driver",       "1.0", "FS",      "Чтение FAT32 разделов",                 "built-in", 1},
    {"osbuilder",  "OS Builder",         "1.0", "Tools",   "Создай свою ОС без кода",               "built-in", 1},
    /* Скачиваемые (из VFS /data/store/) */
    {"hello",      "Hello World",        "1.0", "Dev",     "Example .tar.zst package",              "2 KB",     0},
    {"calc",       "Calculator",         "0.1", "Tools",   "Калькулятор командной строки",          "8 KB",     0},
    {"clock",      "Clock Widget",       "1.0", "DE",      "Виджет часов для рабочего стола",       "4 KB",     0},
    {NULL, NULL, NULL, NULL, NULL, NULL, 0}
};

static int store_sel = 0;
static int store_count = 0;
static const char *filter_cat = NULL;

static void store_count_pkgs(void) {
    store_count = 0;
    for (int i = 0; catalog[i].id; i++) store_count++;
}

static void store_draw(void) {
    terminal_clear();

    /* Header */
    terminal_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    terminal_writeln("                                                                                ");
    terminal_write(" Exp Store — atmkoala v0.5                                  ");
    terminal_writeln("");
    terminal_writeln("                                                                                ");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_writeln("");

    /* Column headers */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_write("  "); terminal_write("NAME                ");
    terminal_write("VER   "); terminal_write("CAT       ");
    terminal_writeln("DESCRIPTION");
    terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_writeln("  ──────────────────────────────────────────────────────────────────────────");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int shown = 0;
    int scroll_start = (store_sel > 12) ? store_sel - 12 : 0;

    for (int i = 0; catalog[i].id && shown < 14; i++) {
        if (i < scroll_start) continue;
        int selected = (i == store_sel);

        if (selected) terminal_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
        else if (catalog[i].installed) terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        else terminal_set_color(VGA_WHITE, VGA_BLACK);

        terminal_write(selected ? " >" : "  ");
        char line[64];
        kstrcpy(line, catalog[i].name);
        while ((int)kstrlen(line) < 20) kstrcat(line, " ");
        line[20] = 0; terminal_write(line);

        kstrcpy(line, catalog[i].version);
        while ((int)kstrlen(line) < 6) kstrcat(line, " ");
        line[6] = 0; terminal_write(line);

        kstrcpy(line, catalog[i].category);
        while ((int)kstrlen(line) < 10) kstrcat(line, " ");
        line[10] = 0; terminal_write(line);

        terminal_writeln(catalog[i].desc);
        shown++;
    }

    /* Selected pkg detail */
    terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_writeln("\n  ──────────────────────────────────────────────────────────────────────────");
    if (store_sel < store_count) {
        store_pkg_t *p = &catalog[store_sel];
        terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        terminal_write("  "); terminal_write(p->name);
        terminal_write(" v"); terminal_write(p->version);
        terminal_write("  ["); terminal_write(p->category); terminal_write("]");
        terminal_write("  Size: "); terminal_writeln(p->size);
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        terminal_write("  Status: ");
        if (p->installed) {
            terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            terminal_writeln("Installed ✓");
        } else {
            terminal_set_color(VGA_YELLOW, VGA_BLACK);
            terminal_writeln("Not installed");
        }
    }
    terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_writeln("  ↑↓=navigate  Enter=install  I=info  Q=quit");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void store_install_pkg(store_pkg_t *p) {
    if (p->installed) {
        terminal_set_color(VGA_YELLOW, VGA_BLACK);
        terminal_write("  "); terminal_write(p->name);
        terminal_writeln(" уже установлен.");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        pit_sleep(1000);
        return;
    }
    /* Prefer ATPK: native control+manifest+payload package. Legacy .tar.zst
     * is accepted only for existing catalogue content during migration. */
    char path[64]; kstrcpy(path, "/data/store/");
    kstrcat(path, p->id); kstrcat(path, ".atpk");
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kstrcpy(path, "/data/store/"); kstrcat(path, p->id); kstrcat(path, ".tar.zst");
        fd = vfs_open(path, O_RDONLY, 0);
    }
    if (fd < 0) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        terminal_write("  Файл не найден: "); terminal_writeln(path);
        terminal_write("  Поместите "); terminal_write(p->id);
        terminal_writeln(".atpk (preferred) or .tar.zst in /data/store/ and try again.");
        terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
        terminal_writeln("  (в реальной системе здесь был бы HTTP download)");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        pit_sleep(2000);
        return;
    }
    static uint8_t buf[TZST_MAX_UNPACKED + 4096]; int n = vfs_read(fd, buf, sizeof(buf)); vfs_close(fd);
    tzst_pkg_t pkg;
    if (n < 0 || tzst_parse(&pkg, buf, (uint32_t)n) < 0) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        terminal_writeln("  Invalid or unsupported ATPK/.tar.zst package.");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        pit_sleep(1500);
        return;
    }
    tzst_install(&pkg);
    p->installed = 1;
    pit_sleep(1000);
}

void store_run(void) {
    store_count_pkgs();
    store_sel = 0;

    while (1) {
        store_draw();
        int k = keyboard_getkey();
        if (k == 'q' || k == 'Q' || k == 27) break;
        if (k == KEY_UP   && store_sel > 0) store_sel--;
        if (k == KEY_DOWN && store_sel < store_count-1) store_sel++;
        if (k == '\n' || k == '\r') store_install_pkg(&catalog[store_sel]);
        if (k == 'i' || k == 'I') {
            terminal_clear();
            store_pkg_t *p = &catalog[store_sel];
            terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            terminal_write("  Package: "); terminal_writeln(p->name);
            terminal_write("  ID:      "); terminal_writeln(p->id);
            terminal_write("  Version: "); terminal_writeln(p->version);
            terminal_write("  Category:"); terminal_writeln(p->category);
            terminal_write("  Size:    "); terminal_writeln(p->size);
            terminal_write("  Status:  "); terminal_writeln(p->installed?"Installed":"Available");
            terminal_write("  Desc:    "); terminal_writeln(p->desc);
            terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
            terminal_writeln("\n  Press any key...");
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            keyboard_getkey();
        }
    }
    terminal_clear();
}
