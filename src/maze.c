/* maze.c — Maze Explorer — atmkoala v0.5
 * Генератор лабиринта рекурсивным backtracking + игровой цикл
 */
#include "maze.h"
#include "gamesdk.h"
#include "vga.h"
#include "keyboard.h"
#include "sched.h"
#include "util.h"
#include <stdint.h>

#define MZ_W   39   /* нечётное! */
#define MZ_H   19   /* нечётное! */
#define MZ_CX   2
#define MZ_CY   1

static uint8_t mz_grid[MZ_H][MZ_W];  /* 0=wall 1=path 2=visited */
static uint32_t mz_rand;

static uint32_t mz_rng(void) {
    mz_rand = mz_rand * 1664525u + 1013904223u;
    return mz_rand;
}

/* Shuffle array of 4 directions */
static void shuffle4(int *arr) {
    for (int i = 3; i > 0; i--) {
        int j = (int)(mz_rng() % (uint32_t)(i+1));
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

/* Recursive DFS maze carver */
static void mz_carve(int cx, int cy) {
    int dirs[4] = {0,1,2,3};
    shuffle4(dirs);
    int dx[] = {0,0,-2,2};
    int dy[] = {-2,2,0,0};
    for (int d = 0; d < 4; d++) {
        int nx = cx + dx[dirs[d]];
        int ny = cy + dy[dirs[d]];
        if (nx>0&&nx<MZ_W-1&&ny>0&&ny<MZ_H-1&&mz_grid[ny][nx]==0) {
            mz_grid[(cy+ny)/2][(cx+nx)/2] = 1;
            mz_grid[ny][nx] = 1;
            mz_carve(nx, ny);
        }
    }
}

static void mz_generate(void) {
    mz_rand = sched_uptime_ticks() ^ 0xDEAD1234;
    /* Fill with walls */
    for (int r=0;r<MZ_H;r++) for (int c=0;c<MZ_W;c++) mz_grid[r][c]=0;
    /* Start */
    mz_grid[1][1] = 1;
    mz_carve(1, 1);
    /* Exit */
    mz_grid[MZ_H-2][MZ_W-2] = 1;
    mz_grid[MZ_H-2][MZ_W-1] = 1;  /* exit hole */
}

static void mz_draw(int px, int py, int steps, int fog) {
    for (int r=0;r<MZ_H;r++) {
        terminal_set_cursor(MZ_CY+r, MZ_CX);
        for (int c=0;c<MZ_W;c++) {
            /* Fog of war */
            int dist = (px-c)*(px-c)+(py-r)*(py-r);
            if (fog && dist > 64) {
                terminal_set_color(VGA_BLACK, VGA_BLACK);
                terminal_putchar(' ');
                continue;
            }
            if (r==py&&c==px) {
                terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                terminal_putchar('@');
            } else if (r==MZ_H-2&&c==MZ_W-1) {
                terminal_set_color(VGA_YELLOW, VGA_BLACK);
                terminal_putchar('E');
            } else if (mz_grid[r][c]==0) {
                terminal_set_color(VGA_BLUE, VGA_BLUE);
                terminal_putchar(' ');
            } else {
                terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
                terminal_putchar('.');
            }
        }
    }
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_set_cursor(MZ_CY+MZ_H, MZ_CX);
    char sb[64]; kstrcpy(sb,"Steps:"); kitoa(steps,sb+6,10);
    while((int)kstrlen(sb)<20)kstrcat(sb," ");
    kstrcat(sb,"WASD/Arrows=move  F=fog  Q=quit");
    terminal_write(sb);
}

void game_maze(void) {
    game_screen_title("MAZE EXPLORER", "Найди выход!  WASD/Arrows=move  F=fog  Q=quit");
    pit_sleep(1500);

    mz_generate();
    int px=1, py=1, steps=0, fog=1;
    int won=0;

    game_clear();

    while (1) {
        mz_draw(px, py, steps, fog);

        int k = game_key_wait();
        if (game_key_quit(k)) break;
        if (k=='f'||k=='F') { fog=!fog; continue; }

        int nx=px, ny=py;
        if (k==KEY_UP   ||k=='w'||k=='W') ny--;
        if (k==KEY_DOWN ||k=='s'||k=='S') ny++;
        if (k==KEY_LEFT ||k=='a'||k=='A') nx--;
        if (k==KEY_RIGHT||k=='d'||k=='D') nx++;

        if (nx>=0&&nx<MZ_W&&ny>=0&&ny<MZ_H&&mz_grid[ny][nx]!=0) {
            px=nx; py=ny; steps++;
        }

        /* Win: reach exit */
        if (px==MZ_W-1&&py==MZ_H-2) { won=1; break; }
    }

    game_clear();
    if (won) {
        game_beep(880, 100); game_beep(1100, 150);
        game_screen_win(1000 - steps*2 > 0 ? 1000 - steps*2 : 10);
    }
    game_exit();
}
