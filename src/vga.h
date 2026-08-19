#ifndef VGA_H
#define VGA_H
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define SCROLLBACK_LINES 500

/* VGA colors */
typedef enum {
    VGA_BLACK=0,VGA_BLUE=1,VGA_GREEN=2,VGA_CYAN=3,
    VGA_RED=4,VGA_MAGENTA=5,VGA_BROWN=6,VGA_LIGHT_GREY=7,
    VGA_DARK_GREY=8,VGA_LIGHT_BLUE=9,VGA_LIGHT_GREEN=10,VGA_LIGHT_CYAN=11,
    VGA_LIGHT_RED=12,VGA_LIGHT_MAGENTA=13,VGA_YELLOW=14,VGA_WHITE=15,
} vga_color_t;

/* Color scheme */
typedef struct {
    uint8_t normal_fg, normal_bg;
    uint8_t prompt_fg, error_fg;
    uint8_t accent_fg, header_fg;
    uint8_t header_bg, success_fg;
    uint8_t warn_fg,   dim_fg;
    const char *name;
} color_scheme_t;

typedef enum {
    SCHEME_CARAMEL=0, SCHEME_MATRIX, SCHEME_OCEAN, SCHEME_NORD,
    SCHEME_RETRO, SCHEME_DRACULA, SCHEME_SOLARIZED, SCHEME_GRUVBOX,
    SCHEME_COUNT
} scheme_id_t;

/* Init / clear */
void terminal_init(void);
void terminal_clear(void);

/* Color */
void terminal_set_color(uint8_t fg, uint8_t bg);
void terminal_set_scheme(scheme_id_t id);
scheme_id_t           terminal_get_scheme(void);
const color_scheme_t *terminal_current_scheme(void);

/* Output */
void terminal_putchar(char c);       /* '\b' = move left only, no erase */
void terminal_erase_char(void);      /* erase under cursor, don't move */
void terminal_erase_eol(void);       /* erase from cursor to end of line */
void terminal_write(const char *s);
void terminal_write_utf8(const char *s);  /* UTF-8 -> VGA transliteration */
void terminal_writeln(const char *s);

/* Cursor */
void terminal_set_cursor(int row, int col);
void terminal_get_cursor(int *row, int *col);
void terminal_save_cursor(void);
void terminal_restore_cursor(void);

/* Scrollback */
void terminal_scroll_up(int lines);
void terminal_scroll_down(int lines);
void terminal_scroll_to_bottom(void);
int  terminal_get_scroll_pos(void);

/* Decorations */
void terminal_print_logo(void);
void terminal_draw_statusbar(const char *left, const char *right);
void terminal_draw_hline(int row, char c, uint8_t color);

/* Convenience macros */
#define SCH  terminal_current_scheme()
#define SCH_NRM terminal_set_color(SCH->normal_fg,  SCH->normal_bg)
#define SCH_ERR terminal_set_color(SCH->error_fg,   SCH->normal_bg)
#define SCH_ACC terminal_set_color(SCH->accent_fg,  SCH->normal_bg)
#define SCH_OK  terminal_set_color(SCH->success_fg, SCH->normal_bg)
#define SCH_HDR terminal_set_color(SCH->header_fg,  SCH->normal_bg)
#define SCH_WRN terminal_set_color(SCH->warn_fg,    SCH->normal_bg)
#define SCH_DIM terminal_set_color(SCH->dim_fg,     SCH->normal_bg)
#define SCH_PRM terminal_set_color(SCH->prompt_fg,  SCH->normal_bg)

#endif
