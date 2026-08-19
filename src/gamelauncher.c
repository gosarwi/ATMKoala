/* gamelauncher.c — Game Launcher — atmkoala v0.5 */
#include "gamelauncher.h"
#include "gamesdk.h"
#include "minesweeper.h"
#include "snake_game.h"
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "sched.h"
#include "util.h"

extern void game_snake(void);
extern void game_tetris(void);
extern void game_pong(void);
extern void game_breakout(void);

typedef struct {
    const char *name;
    const char *desc;
    const char *controls;
    void (*fn)(void);
} launcher_game_t;

static const launcher_game_t games[] = {
    {"Snake",        "Змейка — собирай еду, не врезайся",    "Arrows=move Q=quit",      game_snake_modern},
    {"Minesweeper",  "Сапёр — открой поле и найди мины",      "Arrows=move Enter=open Space=flag", game_minesweeper},
    {"Tetris",       "Собирай линии из падающих фигур",      "Arrows=move Space=drop Q=quit", game_tetris},
    {"Pong",         "Классический теннис — 2 игрока",       "W/S=left  Up/Dn=right",   game_pong},
    {"Breakout",     "Разбей все кирпичи шариком",           "Arrows=paddle Space=launch", game_breakout},
    {NULL, NULL, NULL, NULL}
};

void gamelauncher_run(void) {
    int sel = 0, count = 0;
    while (games[count].name) count++;

    while (1) {
        terminal_clear();

        /* Header */
        terminal_set_color(VGA_BLACK, VGA_LIGHT_MAGENTA);
        terminal_writeln("                                                                                ");
        terminal_writeln("   🎮  atmkoala Game Launcher v0.5                                            ");
        terminal_writeln("                                                                                ");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        terminal_writeln("");

        for (int i = 0; i < count; i++) {
            if (i == sel) {
                terminal_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
                terminal_write(" > ");
            } else {
                terminal_set_color(VGA_WHITE, VGA_BLACK);
                terminal_write("   ");
            }

            terminal_write(games[i].name);
            /* Pad */
            int pad = 18 - (int)kstrlen(games[i].name);
            for (int p=0;p<pad;p++) terminal_putchar(' ');
            terminal_set_color(i==sel?VGA_DARK_GREY:VGA_DARK_GREY, i==sel?VGA_LIGHT_CYAN:VGA_BLACK);
            terminal_write(games[i].desc);
            terminal_writeln("");
        }

        terminal_writeln("");

        /* Selected game details */
        if (sel < count) {
            terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
            terminal_writeln("  ──────────────────────────────────────────");
            terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            terminal_write("  Controls: ");
            terminal_set_color(VGA_WHITE, VGA_BLACK);
            terminal_writeln(games[sel].controls);
        }

        terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
        terminal_writeln("\n  ↑↓=выбор  Enter=играть  Q=выход");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

        int k = keyboard_getkey();
        if (k=='q'||k=='Q'||k==27) break;
        if (k==KEY_UP && sel>0) sel--;
        if (k==KEY_DOWN && sel<count-1) sel++;
        if ((k=='\n'||k=='\r') && games[sel].fn) {
            terminal_clear();
            games[sel].fn();
            terminal_clear();
        }
    }
    terminal_clear();
}
