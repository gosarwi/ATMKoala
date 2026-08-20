#ifndef VBE_H
#define VBE_H

#include <stdint.h>
#include <stddef.h>

/* VBE mode info block (filled by BIOS / GRUB) */
typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t  win_a, win_b;
    uint16_t granularity, win_size;
    uint16_t seg_a, seg_b;
    uint32_t win_func_ptr;
    uint16_t pitch;          /* bytes per scanline */
    uint16_t width;          /* horizontal resolution */
    uint16_t height;         /* vertical resolution */
    uint8_t  w_char, y_char, planes, bpp, banks;
    uint8_t  memory_model, bank_size, image_pages;
    uint8_t  reserved0;
    /* direct color fields */
    uint8_t  red_mask, red_pos;
    uint8_t  green_mask, green_pos;
    uint8_t  blue_mask, blue_pos;
    uint8_t  rsvd_mask, rsvd_pos;
    uint8_t  direct_color_attrs;
    uint32_t framebuffer;    /* physical LFB address */
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_sz;
    uint8_t  reserved1[206];
} vbe_mode_info_t;

/* Multiboot framebuffer info (from multiboot header flags bit 2) */
typedef struct {
    uint32_t addr_lo, addr_hi;
    uint32_t pitch;
    uint32_t width, height;
    uint8_t  bpp;
    uint8_t  type;        /* 1 = RGB, 2 = EGA text */
    uint16_t reserved;
    /* RGB color info for type=1 */
    uint8_t  red_pos, red_mask;
    uint8_t  green_pos, green_mask;
    uint8_t  blue_pos, blue_mask;
} mb_fb_info_t;

/* Simple 32-bit ARGB color */
typedef uint32_t color32_t;
#define RGB(r,g,b)  ((color32_t)(((r)<<16)|((g)<<8)|(b)))
#define RGBA(r,g,b,a) ((color32_t)(((a)<<24)|((r)<<16)|((g)<<8)|(b)))

/* VBE state */
typedef struct {
    uint32_t *fb;       /* framebuffer pointer */
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;    /* bytes per row */
    uint8_t   bpp;
    int       active;   /* 1 = LFB mode, 0 = VGA text mode */
} vbe_state_t;

extern vbe_state_t vbe;

/* Init from multiboot framebuffer tag */
int  vbe_init(mb_fb_info_t *fb_info);

/* Drawing primitives */
void vbe_putpixel(int x, int y, color32_t c);

/* Optional software back buffer.  Exp renders a complete frame off-screen
 * and presents it atomically to avoid visible erase/redraw flicker. */
int  vbe_double_buffer_enable(void);
void vbe_double_buffer_disable(void);
void vbe_present(void);
void vbe_fill_rect(int x, int y, int w, int h, color32_t c);
/* Blend a colour over the active VBE draw surface. Suitable for compact UI
 * overlays; full background blur needs a retained compositor and is absent. */
void vbe_blend_rect(int x, int y, int w, int h, color32_t c, uint8_t alpha);
void vbe_draw_hline(int x, int y, int len, color32_t c);
void vbe_draw_vline(int x, int y, int len, color32_t c);
void vbe_draw_rect(int x, int y, int w, int h, color32_t c);
void vbe_clear(color32_t c);
/* Nearest-neighbour RGBA blit to the active draw surface (backbuffer when
 * enabled). Used for cached desktop wallpapers; source pixels are RGBA. */
void vbe_blit_rgba_scaled(const uint8_t *rgba,int src_w,int src_h,int dst_x,int dst_y,int dst_w,int dst_h);

/* Font rendering (8x16 bitmap font) */
void vbe_putchar(int x, int y, char c, color32_t fg, color32_t bg);
void vbe_putchar_cp(int x, int y, uint32_t codepoint, color32_t fg, color32_t bg);
void vbe_puts(int x, int y, const char *s, color32_t fg, color32_t bg);

/* Console layer on top of LFB */
void vbe_console_init(void);
void vbe_console_putchar(char c);
void vbe_console_write(const char *s);
void vbe_console_clear(void);
void vbe_console_set_color(color32_t fg, color32_t bg);
/* Editable-line support for framebuffer-backed text mode. Coordinates are
 * character cells; invalid coordinates are safely clamped. */
void vbe_console_get_cursor(int *row, int *col);
void vbe_console_set_cursor(int row, int col);
void vbe_console_erase_eol(void);

/* Logo / boot splash */
void vbe_draw_logo(void);

#endif
