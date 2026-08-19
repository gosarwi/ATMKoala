#ifndef GAMESDK_H
#define GAMESDK_H
/* gamesdk.h — atmkoala v0.5 Mini Game SDK
 * Простой API для разработки игр без знания внутренностей ОС
 */
#include <stdint.h>
#include <stddef.h>

/* ── Инициализация ──────────────────────────────────────── */
void game_init(const char *title);    /* Инициализировать игровой режим */
void game_exit(void);                  /* Выйти, вернуть управление shell */

/* ── Ввод ───────────────────────────────────────────────── */
int  game_key_poll(void);              /* Неблокирующий опрос клавиши */
int  game_key_wait(void);              /* Блокирующий опрос */
int  game_key_quit(int k);             /* Возвращает 1 если Q/Esc/Ctrl+C */

/* ── Текстовый экран (VGA) ──────────────────────────────── */
void game_clear(void);                 /* Очистить экран */
void game_puts(int x, int y, const char *s, int color);
void game_putc(int x, int y, char c, int color);
void game_box(int x, int y, int w, int h, int color);     /* Рамка */
void game_fill(int x, int y, int w, int h, char c, int color);

/* ── Цвета (VGA 0-15) ───────────────────────────────────── */
#define GC_BLACK    0
#define GC_BLUE     1
#define GC_GREEN    2
#define GC_CYAN     3
#define GC_RED      4
#define GC_MAGENTA  5
#define GC_BROWN    6
#define GC_LGREY    7
#define GC_DGREY    8
#define GC_LBLUE    9
#define GC_LGREEN   10
#define GC_LCYAN    11
#define GC_LRED     12
#define GC_LMAGENTA 13
#define GC_YELLOW   14
#define GC_WHITE    15

/* ── Таймер ─────────────────────────────────────────────── */
uint32_t game_ticks(void);             /* Тики с начала игры (100Hz) */
void     game_sleep_ms(uint32_t ms);   /* Задержка */
void     game_wait_frame(uint32_t fps);/* Ждать до следующего кадра */

/* ── Звук (PC Speaker beep) ─────────────────────────────── */
void game_beep(uint32_t freq_hz, uint32_t ms);

/* ── Счёт и рекорды ─────────────────────────────────────── */
void     game_score_set(int score);
int      game_score_get(void);
void     game_score_show(int x, int y, int color);

/* ── Экран Game Over / Win ──────────────────────────────── */
void game_screen_gameover(int score);
void game_screen_win(int score);
void game_screen_title(const char *title, const char *subtitle);

#endif
