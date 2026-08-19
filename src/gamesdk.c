/* gamesdk.c — atmkoala v0.5 Mini Game SDK implementation */
#include "gamesdk.h"
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "sched.h"
#include "util.h"
#include <stdint.h>

extern int use_vbe;

static int   g_score     = 0;
static uint32_t g_frame_last = 0;

void game_init(const char *title) {
    terminal_clear();
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    g_score = 0;
    g_frame_last = sched_uptime_ticks();
    (void)title;
}

void game_exit(void) {
    terminal_clear();
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

int game_key_poll(void) { return keyboard_poll(); }
int game_key_wait(void) { return keyboard_getkey(); }
int game_key_quit(int k) {
    return (k == 'q' || k == 'Q' || k == 27 || k == 3);
}

void game_clear(void) { terminal_clear(); }

void game_puts(int x, int y, const char *s, int color) {
    terminal_set_color((uint8_t)(color & 0xF), (uint8_t)(color >> 4));
    terminal_set_cursor(y, x);
    terminal_write(s);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void game_putc(int x, int y, char c, int color) {
    char s[2] = {c, 0};
    game_puts(x, y, s, color);
}

void game_box(int x, int y, int w, int h, int color) {
    terminal_set_color((uint8_t)(color & 0xF), (uint8_t)(color >> 4));
    /* Top/bottom */
    terminal_set_cursor(y, x); terminal_putchar('+');
    for (int i = 1; i < w-1; i++) terminal_putchar('-');
    terminal_putchar('+');
    terminal_set_cursor(y+h-1, x); terminal_putchar('+');
    for (int i = 1; i < w-1; i++) terminal_putchar('-');
    terminal_putchar('+');
    /* Sides */
    for (int r = 1; r < h-1; r++) {
        terminal_set_cursor(y+r, x);   terminal_putchar('|');
        terminal_set_cursor(y+r, x+w-1); terminal_putchar('|');
    }
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void game_fill(int x, int y, int w, int h, char c, int color) {
    terminal_set_color((uint8_t)(color & 0xF), (uint8_t)(color >> 4));
    for (int r = 0; r < h; r++) {
        terminal_set_cursor(y+r, x);
        for (int col = 0; col < w; col++) terminal_putchar(c);
    }
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

uint32_t game_ticks(void)        { return sched_uptime_ticks(); }
void     game_sleep_ms(uint32_t ms) { pit_sleep(ms); }

void game_wait_frame(uint32_t fps) {
    if (fps == 0) fps = 30;
    uint32_t ticks_per_frame = 100 / fps;
    if (ticks_per_frame == 0) ticks_per_frame = 1;
    while (sched_uptime_ticks() - g_frame_last < ticks_per_frame)
        __asm__ volatile("pause");
    g_frame_last = sched_uptime_ticks();
}

void game_beep(uint32_t freq_hz, uint32_t ms) {
    if (freq_hz == 0) { outb(0x61, inb(0x61) & 0xFC); return; }
    uint32_t div = 1193180 / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)(div >> 8));
    outb(0x61, inb(0x61) | 0x03);
    pit_sleep(ms);
    outb(0x61, inb(0x61) & 0xFC);
}

void game_score_set(int s) { g_score = s; }
int  game_score_get(void)  { return g_score; }

void game_score_show(int x, int y, int color) {
    char buf[32]; kstrcpy(buf, "Score: "); kitoa(g_score, buf+7, 10);
    game_puts(x, y, buf, color);
}

void game_screen_gameover(int score) {
    terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
    terminal_set_cursor(10, 30); terminal_write("  GAME OVER  ");
    terminal_set_cursor(12, 28); terminal_write("  Score: "); char b[16]; kitoa(score,b,10); terminal_write(b);
    terminal_set_cursor(14, 26); terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_write("  Press any key to continue  ");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    keyboard_getkey();
}

void game_screen_win(int score) {
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_set_cursor(10, 30); terminal_write("  YOU WIN!  ");
    terminal_set_cursor(12, 28); terminal_write("  Score: "); char b[16]; kitoa(score,b,10); terminal_write(b);
    terminal_set_cursor(14, 26); terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_write("  Press any key  ");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    keyboard_getkey();
}

void game_screen_title(const char *title, const char *subtitle) {
    terminal_clear();
    int tl = (int)kstrlen(title);
    int sl = (int)kstrlen(subtitle);
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    terminal_set_cursor(8, (80-tl)/2); terminal_write(title);
    terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
    terminal_set_cursor(10, (80-sl)/2); terminal_write(subtitle);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
