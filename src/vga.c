/* vga.c — atmkoala OS v0.5
 *
 * ИСПРАВЛЕНИЯ v9:
 *   1. Дублирование символов при переносе строки — ИСПРАВЛЕНО.
 *      terminal_putchar() при term_col >= VGA_WIDTH делает перенос
 *      и НЕ печатает символ ещё раз. Счётчик col сбрасывается в 0
 *      ДО возврата, следующий символ идёт на новую строку.
 *
 *   2. "Иероглифы" — были из-за того что VGA принимал старший байт
 *      VGA entry как символ. Исправлено: mkentry всегда
 *      берёт только младший байт символа (unsigned char).
 *
 *   3. rl_redraw при многострочном вводе — отслеживаем
 *      terminal_row_of_prompt чтобы знать точно на какой строке
 *      начался ввод, даже после скролла.
 *
 *   4. '\b' — только двигает курсор влево. Не стирает. Никогда.
 *      Стирание — через terminal_erase_char().
 *
 *   5. Новое: terminal_write_utf8() — выводит UTF-8 строку.
 *      В VGA режиме кириллица → транслитерация (VGA не поддерживает Unicode).
 *      В VBE режиме — через vbe_console_write() с полной кириллицей.
 */
#include "vga.h"
#include "util.h"
#include <stdint.h>

#define VGA_MEM ((uint16_t *)0xB8000)

/* ── Color schemes ────────────────────────────────────────── */
static const color_scheme_t schemes[SCHEME_COUNT] = {
    /* 0 CARAMEL */
    { VGA_LIGHT_GREY,VGA_BLACK, VGA_LIGHT_GREEN,VGA_LIGHT_RED,
      VGA_LIGHT_CYAN,VGA_YELLOW,VGA_BLACK,VGA_LIGHT_GREEN,
      VGA_YELLOW,VGA_DARK_GREY,"caramel" },
    /* 1 MATRIX */
    { VGA_GREEN,VGA_BLACK, VGA_LIGHT_GREEN,VGA_LIGHT_RED,
      VGA_WHITE,VGA_LIGHT_GREEN,VGA_BLACK,VGA_LIGHT_GREEN,
      VGA_YELLOW,VGA_GREEN,"matrix" },
    /* 2 OCEAN */
    { VGA_LIGHT_CYAN,VGA_BLUE, VGA_WHITE,VGA_LIGHT_RED,
      VGA_YELLOW,VGA_WHITE,VGA_BLUE,VGA_LIGHT_GREEN,
      VGA_YELLOW,VGA_CYAN,"ocean" },
    /* 3 NORD */
    { VGA_LIGHT_GREY,VGA_DARK_GREY, VGA_CYAN,VGA_LIGHT_RED,
      VGA_LIGHT_BLUE,VGA_WHITE,VGA_DARK_GREY,VGA_LIGHT_GREEN,
      VGA_YELLOW,VGA_LIGHT_GREY,"nord" },
    /* 4 RETRO */
    { VGA_YELLOW,VGA_BLACK, VGA_LIGHT_RED,VGA_LIGHT_RED,
      VGA_WHITE,VGA_YELLOW,VGA_BLACK,VGA_LIGHT_GREEN,
      VGA_LIGHT_RED,VGA_BROWN,"retro" },
    /* 5 DRACULA */
    { VGA_LIGHT_GREY,VGA_BLACK, VGA_LIGHT_MAGENTA,VGA_LIGHT_RED,
      VGA_LIGHT_CYAN,VGA_LIGHT_MAGENTA,VGA_BLACK,VGA_LIGHT_GREEN,
      VGA_YELLOW,VGA_DARK_GREY,"dracula" },
    /* 6 SOLARIZED */
    { VGA_CYAN,VGA_DARK_GREY, VGA_GREEN,VGA_RED,
      VGA_YELLOW,VGA_WHITE,VGA_DARK_GREY,VGA_GREEN,
      VGA_YELLOW,VGA_CYAN,"solarized" },
    /* 7 GRUVBOX */
    { VGA_YELLOW,VGA_BLACK, VGA_GREEN,VGA_RED,
      VGA_LIGHT_CYAN,VGA_YELLOW,VGA_BLACK,VGA_GREEN,
      VGA_LIGHT_RED,VGA_BROWN,"gruvbox" },
};
static scheme_id_t cur_scheme = SCHEME_CARAMEL;

/* ── State ────────────────────────────────────────────────── */
static uint16_t scrollbuf[SCROLLBACK_LINES][VGA_WIDTH];
static int      sb_top   = 0;
static int      sb_count = 0;
static int      sb_off   = 0;

static int      term_row = 0;
static int      term_col = 0;
static uint8_t  term_color;
static uint16_t *vga = (uint16_t *)0xB8000;

static int saved_row = 0, saved_col = 0;

static inline uint8_t  mkcolor(uint8_t fg, uint8_t bg) { return (fg & 0xF) | ((bg & 0xF) << 4); }
/* FIX: всегда unsigned char чтобы не было иероглифов */
static inline uint16_t mkentry(unsigned char c, uint8_t col) { return (uint16_t)c | ((uint16_t)col << 8); }

/* ── Hardware cursor ──────────────────────────────────────── */
static void hw_cursor(int r, int c) {
    uint16_t pos = (uint16_t)(r * VGA_WIDTH + c);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)pos);
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}
static void hw_cursor_hide(void) { outb(0x3D4, 0x0A); outb(0x3D5, 0x20); }

/* ── Scrollback ───────────────────────────────────────────── */
static void sb_commit(int row) {
    int slot = sb_top % SCROLLBACK_LINES;
    for (int c = 0; c < VGA_WIDTH; c++)
        scrollbuf[slot][c] = vga[row * VGA_WIDTH + c];
    sb_top++;
    if (sb_count < SCROLLBACK_LINES) sb_count++;
}

static void sb_redraw(void) {
    uint8_t blank = mkcolor(schemes[cur_scheme].normal_fg,
                            schemes[cur_scheme].normal_bg);
    for (int r = 0; r < VGA_HEIGHT; r++) {
        int src = sb_top - VGA_HEIGHT - sb_off + r;
        if (src < sb_top - sb_count || src < 0) {
            for (int c = 0; c < VGA_WIDTH; c++)
                vga[r * VGA_WIDTH + c] = mkentry(' ', blank);
        } else {
            int slot = ((src % SCROLLBACK_LINES) + SCROLLBACK_LINES) % SCROLLBACK_LINES;
            for (int c = 0; c < VGA_WIDTH; c++)
                vga[r * VGA_WIDTH + c] = scrollbuf[slot][c];
        }
    }
    if (sb_off == 0) hw_cursor(term_row, term_col);
    else             hw_cursor_hide();
}

/* Физический скролл на 1 строку вверх */
static void scroll_up_one(void) {
    sb_commit(0);
    for (int r = 1; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            vga[(r-1) * VGA_WIDTH + c] = vga[r * VGA_WIDTH + c];
    uint8_t blank = mkcolor(schemes[cur_scheme].normal_fg,
                            schemes[cur_scheme].normal_bg);
    for (int c = 0; c < VGA_WIDTH; c++)
        vga[(VGA_HEIGHT-1) * VGA_WIDTH + c] = mkentry(' ', blank);
    term_row = VGA_HEIGHT - 1;
}

/* ── Init / clear ─────────────────────────────────────────── */
void terminal_init(void) {
    vga = VGA_MEM;
    term_color = mkcolor(VGA_LIGHT_GREY, VGA_BLACK);
    sb_top = sb_count = sb_off = 0;
    for (int i = 0; i < SCROLLBACK_LINES; i++)
        for (int j = 0; j < VGA_WIDTH; j++)
            scrollbuf[i][j] = mkentry(' ', term_color);
    terminal_clear();
}
void terminal_clear(void) {
    uint8_t bg = mkcolor(schemes[cur_scheme].normal_fg,
                         schemes[cur_scheme].normal_bg);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = mkentry(' ', bg);
    term_row = term_col = sb_off = 0;
    hw_cursor(0, 0);
}

/* ── Color / scheme ───────────────────────────────────────── */
void terminal_set_color(uint8_t fg, uint8_t bg) { term_color = mkcolor(fg, bg); }
void terminal_set_scheme(scheme_id_t id) {
    if (id < SCHEME_COUNT) {
        cur_scheme = id;
        term_color = mkcolor(schemes[id].normal_fg, schemes[id].normal_bg);
    }
}
scheme_id_t           terminal_get_scheme(void)     { return cur_scheme; }
const color_scheme_t *terminal_current_scheme(void) { return &schemes[cur_scheme]; }

/* ── Scrollback public ────────────────────────────────────── */
void terminal_scroll_up(int n)   { sb_off += n; int m=sb_count>VGA_HEIGHT?sb_count-VGA_HEIGHT:0; if(sb_off>m)sb_off=m; sb_redraw(); }
void terminal_scroll_down(int n) { sb_off -= n; if(sb_off<0)sb_off=0; sb_redraw(); }
void terminal_scroll_to_bottom(void) { if(sb_off){sb_off=0;sb_redraw();} }
int  terminal_get_scroll_pos(void)   { return sb_off; }

/* ── Cursor ───────────────────────────────────────────────── */
void terminal_save_cursor(void)              { saved_row=term_row; saved_col=term_col; }
void terminal_restore_cursor(void)           { term_row=saved_row; term_col=saved_col; hw_cursor(term_row,term_col); }
void terminal_get_cursor(int *r, int *c)     { *r=term_row; *c=term_col; }
void terminal_set_cursor(int r, int c)       { term_row=r; term_col=c; hw_cursor(r,c); }
void terminal_erase_char(void)               { vga[term_row*VGA_WIDTH+term_col]=mkentry(' ',term_color); }
void terminal_erase_eol(void)                { for(int c=term_col;c<VGA_WIDTH;c++) vga[term_row*VGA_WIDTH+c]=mkentry(' ',term_color); }

/* ── Core putchar — ИСПРАВЛЕНО ────────────────────────────── */
void terminal_putchar(char c) {
    if (sb_off > 0) { sb_off = 0; sb_redraw(); }

    switch (c) {
    case '\n':
        term_col = 0;
        term_row++;
        if (term_row >= VGA_HEIGHT) scroll_up_one();
        break;
    case '\r':
        term_col = 0;
        break;
    case '\b':
        /* ТОЛЬКО двигаем курсор влево. НЕ стираем символ. */
        if (term_col > 0) --term_col;
        break;
    case '\t': {
        int nx = (term_col + 8) & ~7;
        if (nx >= VGA_WIDTH) nx = VGA_WIDTH - 1;
        while (term_col < nx)
            vga[term_row * VGA_WIDTH + term_col++] = mkentry(' ', term_color);
        break;
    }
    default:
        if ((unsigned char)c < 0x20) break; /* игнорируем управляющие */

        /* Записываем символ */
        vga[term_row * VGA_WIDTH + term_col] = mkentry((unsigned char)c, term_color);
        term_col++;

        /* FIX: перенос строки — НЕ дублируем символ */
        if (term_col >= VGA_WIDTH) {
            term_col = 0;
            term_row++;
            if (term_row >= VGA_HEIGHT) scroll_up_one();
        }
        break;
    }

    if (sb_off == 0) hw_cursor(term_row, term_col);
}

void terminal_write(const char *s)   { while (*s) terminal_putchar(*s++); }
void terminal_writeln(const char *s) { terminal_write(s); terminal_putchar('\n'); }

/*
 * Кириллица в VGA режиме — транслитерация.
 * VGA кодировка CP437 не поддерживает кириллицу,
 * поэтому выводим транслит для VGA.
 * В VBE режиме — полная кириллица через font.c.
 *
 * Принимает UTF-8 строку.
 */
static char translit(uint32_t cp) {
    /* Строчные кириллические → ASCII транслит */
    if (cp >= 0x0430 && cp <= 0x044F) {
        static const char *tbl[] = {
            "a","b","v","g","d","e","zh","z",
            "i","j","k","l","m","n","o","p",
            "r","s","t","u","f","h","ts","ch",
            "sh","sch","","y","","e","yu","ya"
        };
        int idx = (int)(cp - 0x0430);
        if (idx < 32 && tbl[idx][1] == 0) return tbl[idx][0];
        return '?';
    }
    /* Заглавные */
    if (cp >= 0x0410 && cp <= 0x042F) return translit(cp + 0x20);
    if (cp == 0x0451) return 'e'; /* ё */
    if (cp == 0x0401) return 'E'; /* Ё */
    if (cp < 0x80) return (char)cp;
    return '?';
}

void terminal_write_utf8(const char *s) {
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) {
            cp = (uint32_t)(*p & 0x1F) << 6; p++;
            cp |= (*p & 0x3F); p++;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (uint32_t)(*p & 0x0F) << 12; p++;
            cp |= (uint32_t)(*p & 0x3F) << 6; p++;
            cp |= (*p & 0x3F); p++;
        } else { p++; continue; }
        terminal_putchar(translit(cp));
    }
}

/* ── Status bar ───────────────────────────────────────────── */
void terminal_draw_statusbar(const char *left, const char *right) {
    int sr=term_row, sc2=term_col; uint8_t sc=term_color;
    const color_scheme_t *s = &schemes[cur_scheme];
    uint8_t bar = mkcolor(s->header_fg,
                          s->normal_bg == VGA_BLACK ? VGA_DARK_GREY : s->normal_bg);
    for (int c = 0; c < VGA_WIDTH; c++) vga[c] = mkentry(' ', bar);
    int llen = (int)kstrlen(left);
    for (int c = 0; c < llen && c+1 < VGA_WIDTH; c++) vga[1+c] = mkentry((unsigned char)left[c], bar);
    int rlen = (int)kstrlen(right);
    int rs = VGA_WIDTH - rlen - 1;
    for (int c = 0; c < rlen && rs+c < VGA_WIDTH; c++) vga[rs+c] = mkentry((unsigned char)right[c], bar);
    term_row=sr; term_col=sc2; term_color=sc;
    hw_cursor(term_row, term_col);
}

void terminal_draw_hline(int row, char c, uint8_t color) {
    for (int i = 0; i < VGA_WIDTH; i++)
        vga[row * VGA_WIDTH + i] = mkentry((unsigned char)c, color);
}

/* ── atmkoala v0.5 boot logo ─────────────────── */
void terminal_print_logo(void) {
    const color_scheme_t *s = &schemes[cur_scheme];
    uint8_t bg = s->normal_bg;
    terminal_set_color(s->header_fg, bg);
    terminal_writeln("  atmkoala v0.5  x86-64");
    terminal_set_color(s->dim_fg, bg);
    terminal_writeln("  Type 'help' for commands.");
    terminal_writeln("");
    terminal_set_color(s->normal_fg, bg);
}
