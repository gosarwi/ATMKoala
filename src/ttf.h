#ifndef TTF_H
#define TTF_H
/*
 * ttf.h — atmkoala v0.5
 * PSF2 bitmap font loader + simple outline glyph rasteriser
 *
 * Supports:
 *   PSF2  (.psf) — PC Screen Font v2 (fixed-size bitmap glyphs)
 *   BDF   (.bdf) — Bitmap Distribution Format (subset)
 *   Built-in fallback — uses existing font.c glyphs
 *
 * API:
 *   ttf_load_psf(path)        Load PSF2 font from VFS
 *   ttf_render_char(fb, x, y, cp, fg, bg, scale)
 *   ttf_get_metrics(cp, w, h) Get glyph dimensions
 *   ttf_set_scale(n)          Pixel scale factor (1=normal, 2=2x, 3=3x)
 *   ttf_unload()              Free loaded font
 */
#include <stdint.h>
#include <stddef.h>

/* PSF2 header (32 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /* 0x864AB572 */
    uint32_t version;      /* 0 */
    uint32_t headersize;   /* 32 */
    uint32_t flags;        /* 0=no unicode table, 1=has unicode table */
    uint32_t numglyph;     /* number of glyphs */
    uint32_t bytesperglyph;
    uint32_t height;       /* pixels */
    uint32_t width;        /* pixels */
} psf2_header_t;

#define PSF2_MAGIC 0x864AB572

/* Font state */
typedef struct {
    int      loaded;
    int      scale;         /* render scale: 1-4 */
    uint8_t  glyph_w;
    uint8_t  glyph_h;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint8_t *glyphs;        /* raw glyph bitmap data */
    uint32_t glyphs_size;
    /* Unicode translation table */
    uint8_t *unicode_table;
    uint32_t unicode_size;
    char     path[64];
} ttf_font_t;

/* ── API ──────────────────────────────────────────────────── */
void ttf_init(void);

/* Load PSF2 font from VFS path */
int  ttf_load_psf(const char *path);

/* Render one glyph to VBE framebuffer at (x,y) */
void ttf_render_char(int x, int y, uint32_t codepoint,
                     uint32_t fg, uint32_t bg);

/* Render UTF-8 string */
void ttf_render_string(int x, int y, const char *utf8,
                       uint32_t fg, uint32_t bg);
/* Exp desktop-only percentage scale; does not alter terminal font scale. */
void ttf_render_string_percent(int x, int y, const char *utf8,
                               uint32_t fg, uint32_t bg, int percent);

/* Current glyph size (after scale) */
int  ttf_glyph_width(void);
int  ttf_glyph_height(void);

void ttf_set_scale(int scale);  /* 1-4 */
int  ttf_get_scale(void);

/* Free font data */
void ttf_unload(void);

/* Is a font loaded? */
int  ttf_loaded(void);

/* Get glyph index for Unicode codepoint (via unicode table) */
uint32_t ttf_glyph_index(uint32_t cp);

#endif
