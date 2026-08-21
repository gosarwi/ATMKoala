/* ttf.c — atmkoala v0.5 — PSF2 font loader
 *
 * Loads PSF2 bitmap fonts from VFS and renders them to VBE framebuffer.
 * Falls back to built-in font.c glyphs if no PSF2 font is loaded.
 *
 * PSF2 format:
 *   - 32-byte header
 *   - numglyph × bytesperglyph bytes of bitmap data
 *   - Optional: Unicode translation table (maps codepoints → glyph indices)
 *
 * Each glyph is height rows of ceil(width/8) bytes.
 * MSB of each byte = leftmost pixel.
 */
#include "ttf.h"
#include "vbe.h"
#include "vfs.h"
#include "kmalloc.h"
#include "util.h"
#include "font.h"
#include "kernel_panic.h"
#include <stdint.h>
#include <stddef.h>

static ttf_font_t font_state;

void ttf_init(void) {
    kmemset(&font_state, 0, sizeof(font_state));
    font_state.scale = 1;
}

/* ── PSF2 loader ──────────────────────────────────────────── */
int ttf_load_psf(const char *path) {
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        EDN_FMT("ttf: cannot open '%s'", path);
        return -1;
    }

    /* Read header */
    psf2_header_t hdr;
    int n = vfs_read(fd, (uint8_t*)&hdr, sizeof(hdr));
    if (n < (int)sizeof(hdr)) {
        vfs_close(fd);
        EDN("ttf: file too small for PSF2 header");
        return -2;
    }

    if (hdr.magic != PSF2_MAGIC) {
        vfs_close(fd);
        EDN_FMT("ttf: bad magic 0x%x (not PSF2)", hdr.magic);
        return -3;
    }

    /* Validate */
    if (hdr.numglyph == 0 || hdr.bytesperglyph == 0 ||
        hdr.width == 0 || hdr.height == 0) {
        vfs_close(fd);
        EDN("ttf: invalid PSF2 dimensions");
        return -4;
    }

    /* Free old font if any */
    ttf_unload();

    uint32_t glyph_data_size = hdr.numglyph * hdr.bytesperglyph;

    /* Allocate glyph buffer */
    font_state.glyphs = (uint8_t*)kmalloc(glyph_data_size);
    if (!font_state.glyphs) {
        vfs_close(fd);
        EDN("ttf: out of memory for glyph data");
        return -5;
    }

    /* Seek to glyph data (after header) and read */
    /* Read header padding bytes if headersize > 32 */
    if (hdr.headersize > (uint32_t)sizeof(hdr)) {
        uint32_t pad = hdr.headersize - (uint32_t)sizeof(hdr);
        uint8_t tmp[64];
        while (pad > 0) {
            int rd = (int)(pad > 64 ? 64 : pad);
            vfs_read(fd, tmp, (uint32_t)rd);
            pad -= (uint32_t)rd;
        }
    }

    /* Read glyph bitmaps */
    int got = vfs_read(fd, font_state.glyphs, glyph_data_size);
    if (got < (int)glyph_data_size) {
        /* Partial read — zero fill rest */
        kmemset(font_state.glyphs + got, 0, glyph_data_size - (uint32_t)got);
        EDN_FMT("ttf: partial glyph read %d/%u bytes", got, glyph_data_size);
    }

    /* Read Unicode table if present (flag bit 0) */
    font_state.unicode_table = NULL;
    font_state.unicode_size  = 0;
    if (hdr.flags & 1) {
        /* Table follows glyph data */
        /* For simplicity: read remaining bytes */
        static uint8_t utbuf[8192];
        int ul = vfs_read(fd, utbuf, sizeof(utbuf)-1);
        if (ul > 0) {
            font_state.unicode_table = (uint8_t*)kmalloc((uint32_t)ul);
            if (font_state.unicode_table) {
                kmemcpy(font_state.unicode_table, utbuf, (uint32_t)ul);
                font_state.unicode_size = (uint32_t)ul;
            }
        }
    }

    vfs_close(fd);

    font_state.loaded        = 1;
    font_state.glyph_w       = (uint8_t)hdr.width;
    font_state.glyph_h       = (uint8_t)hdr.height;
    font_state.numglyph      = hdr.numglyph;
    font_state.bytesperglyph = hdr.bytesperglyph;
    font_state.glyphs_size   = glyph_data_size;
    kstrncpy(font_state.path, path, 63);

    return 0;
}

/* ── Unicode table lookup ─────────────────────────────────── */
uint32_t ttf_glyph_index(uint32_t cp) {
    if (!font_state.loaded) return cp < 256 ? cp : '?';
    if (cp < font_state.numglyph) return cp; /* ASCII direct map */

    /* PSF2 unicode table format:
     * For each glyph index 0..numglyph-1:
     *   sequence of UTF-8 codepoints, terminated by 0xFF
     *   0xFE = start of sequence (equivalents)
     */
    if (!font_state.unicode_table) {
        /* No table — direct index, clamp */
        return cp < font_state.numglyph ? cp : ('?' < font_state.numglyph ? '?' : 0);
    }

    uint8_t *p   = font_state.unicode_table;
    uint8_t *end = p + font_state.unicode_size;
    uint32_t glyph_idx = 0;

    while (p < end && glyph_idx < font_state.numglyph) {
        while (p < end && *p != 0xFF) {
            if (*p == 0xFE) { p++; continue; } /* skip sequence marker */
            /* Decode UTF-8 codepoint */
            uint32_t entry_cp;
            uint8_t b = *p;
            if ((b & 0x80) == 0)      { entry_cp = b; p++; }
            else if ((b & 0xE0)==0xC0) { entry_cp=(b&0x1F)<<6; p++;
                                          entry_cp|=(*p&0x3F); p++; }
            else if ((b & 0xF0)==0xE0) { entry_cp=(b&0x0F)<<12; p++;
                                          entry_cp|=(*p&0x3F)<<6; p++;
                                          entry_cp|=(*p&0x3F); p++; }
            else { p++; continue; }

            if (entry_cp == cp) return glyph_idx;
        }
        if (p < end && *p == 0xFF) p++; /* skip terminator */
        glyph_idx++;
    }

    /* Fallback: try direct index */
    return '?' < font_state.numglyph ? (uint32_t)'?' : 0;
}

/* ── Render single glyph to VBE framebuffer ──────────────── */
void ttf_render_char(int x, int y, uint32_t codepoint,
                     uint32_t fg, uint32_t bg) {
    extern vbe_state_t vbe;
    if (!vbe.active) return;

    /* Use built-in bitmap font if no PSF2 loaded */
    if (!font_state.loaded) {
        vbe_putchar_cp(x, y, codepoint, fg, bg);
        return;
    }

    uint32_t idx = ttf_glyph_index(codepoint);
    if (idx >= font_state.numglyph) idx = '?' < font_state.numglyph ? '?' : 0;

    uint8_t *glyph = font_state.glyphs + idx * font_state.bytesperglyph;

    int gw = font_state.glyph_w;
    int gh = font_state.glyph_h;
    int s  = font_state.scale;
    int bytes_per_row = ((int)gw + 7) / 8;

    for (int row = 0; row < gh; row++) {
        for (int col = 0; col < gw; col++) {
            uint8_t byte = glyph[row * bytes_per_row + col / 8];
            int bit  = 7 - (col % 8);
            uint32_t color = (byte >> bit) & 1 ? fg : bg;
            /* Scale: draw s×s block per pixel */
            for (int sy = 0; sy < s; sy++)
                for (int sx = 0; sx < s; sx++)
                    vbe_putpixel(x + col*s + sx, y + row*s + sy, color);
        }
    }
}

/* ── Render UTF-8 string ──────────────────────────────────── */
void ttf_render_string(int x, int y, const char *utf8,
                       uint32_t fg, uint32_t bg) {
    const uint8_t *p = (const uint8_t *)utf8;
    int cx = x;
    int cw = ttf_glyph_width();
    while (*p) {
        /* Decode one codepoint */
        uint32_t cp;
        uint8_t b = *p;
        if ((b & 0x80) == 0)       { cp = b; p++; }
        else if ((b & 0xE0)==0xC0) { cp=(b&0x1F)<<6; p++; cp|=(*p&0x3F); p++; }
        else if ((b & 0xF0)==0xE0) { cp=(b&0x0F)<<12; p++;
                                      cp|=(*p&0x3F)<<6; p++;
                                      cp|=(*p&0x3F); p++; }
        else if ((b & 0xF8)==0xF0) { cp=(b&0x07)<<18; p++;
                                      cp|=(*p&0x3F)<<12; p++;
                                      cp|=(*p&0x3F)<<6; p++;
                                      cp|=(*p&0x3F); p++; }
        else { cp='?'; p++; }

        if (cp == '\n') {
            cx = x; y += ttf_glyph_height(); continue;
        }
        ttf_render_char(cx, y, cp, fg, bg);
        cx += cw;
    }
}

/* ── Bounded UTF-8 rendering ───────────────────────────────── */
int ttf_render_string_clipped(int x,int y,const char *utf8,uint32_t fg,uint32_t bg,int max_width){
    if(!utf8||max_width<=0)return 0;
    const uint8_t *p=(const uint8_t *)utf8;int cw=ttf_glyph_width(),drawn=0;
    if(cw<=0)return 0;
    while(*p&&*p!='\n'&&(drawn+1)*cw<=max_width){
        uint32_t cp=utf8_decode(&p);
        if(cp=='\n')break;
        ttf_render_char(x+drawn*cw,y,cp,fg,bg);drawn++;
    }
    return drawn;
}

int ttf_render_string_wrapped(int x,int y,const char *utf8,uint32_t fg,uint32_t bg,int max_width,int max_rows){
    if(!utf8||max_width<=0||max_rows<=0)return 0;
    int cw=ttf_glyph_width(),ch=ttf_glyph_height();if(cw<=0||ch<=0)return 0;
    int cols=max_width/cw;if(cols<1)return 0;
    const uint8_t *p=(const uint8_t *)utf8;int row=1,col=0,space_pending=0,rendered=0;
    while(*p&&row<=max_rows){
        if(*p=='\n'){
            p++;if(row>=max_rows)break;row++;y+=ch;col=0;space_pending=0;continue;
        }
        if(*p==' '||*p=='\t'){if(col>0)space_pending=1;p++;continue;}
        const uint8_t *word=p,*q=p;int word_cols=0;
        while(*q&&*q!='\n'&&*q!=' '&&*q!='\t'){(void)utf8_decode(&q);word_cols++;}
        if(col>0&&space_pending){
            if(col+1+word_cols>cols){if(row>=max_rows)break;row++;y+=ch;col=0;}
            else {ttf_render_char(x+col*cw,y,' ',fg,bg);col++;rendered++;}
        }else if(col>0&&col+word_cols>cols){
            if(row>=max_rows)break;row++;y+=ch;col=0;
        }
        while(p<q){
            if(col>=cols){if(row>=max_rows)return row;row++;y+=ch;col=0;}
            uint32_t cp=utf8_decode(&p);ttf_render_char(x+col*cw,y,cp,fg,bg);col++;rendered++;
        }
        space_pending=0;
    }
    return rendered?row:0;
}

/* ── Exp percentage renderer ──────────────────────────────── */
static void ttf_render_char_percent(int x,int y,uint32_t cp,uint32_t fg,uint32_t bg,int percent){
    if(percent<50)percent=50;if(percent>200)percent=200;
    int gw,gh,bpr;const uint8_t *glyph;
    if(font_state.loaded){
        uint32_t idx=ttf_glyph_index(cp);if(idx>=font_state.numglyph)idx='?'<font_state.numglyph?'?':0;
        glyph=font_state.glyphs+idx*font_state.bytesperglyph;gw=font_state.glyph_w;gh=font_state.glyph_h;bpr=(gw+7)/8;
    }else{glyph=font_get_glyph(cp);gw=8;gh=16;bpr=1;}
    int base=font_state.scale;
    for(int row=0;row<gh;row++)for(int col=0;col<gw;col++){
        uint8_t byte=glyph[row*bpr+col/8];uint32_t c=((byte>>(7-(col%8)))&1)?fg:bg;
        int x0=(col*base*percent)/100,x1=((col+1)*base*percent+99)/100;
        int y0=(row*base*percent)/100,y1=((row+1)*base*percent+99)/100;
        if(x1<=x0)x1=x0+1;if(y1<=y0)y1=y0+1;
        vbe_fill_rect(x+x0,y+y0,x1-x0,y1-y0,c);
    }
}
void ttf_render_string_percent(int x,int y,const char *utf8,uint32_t fg,uint32_t bg,int percent){
    const uint8_t *p=(const uint8_t*)utf8;int cx=x;
    int cw=((font_state.loaded?font_state.glyph_w:8)*font_state.scale*percent+99)/100;
    int ch=((font_state.loaded?font_state.glyph_h:16)*font_state.scale*percent+99)/100;
    while(*p){uint32_t cp=utf8_decode(&p);if(cp=='\n'){cx=x;y+=ch;continue;}ttf_render_char_percent(cx,y,cp,fg,bg,percent);cx+=cw;}
}

/* ── Metrics ──────────────────────────────────────────────── */
int ttf_glyph_width(void) {
    if (!font_state.loaded) return 8 * font_state.scale;
    return (int)font_state.glyph_w * font_state.scale;
}
int ttf_glyph_height(void) {
    if (!font_state.loaded) return 16 * font_state.scale;
    return (int)font_state.glyph_h * font_state.scale;
}
void ttf_set_scale(int s) { if(s>=1&&s<=4) font_state.scale=s; }
int  ttf_get_scale(void)  { return font_state.scale; }
int  ttf_loaded(void)     { return font_state.loaded; }

/* ── Unload ───────────────────────────────────────────────── */
void ttf_unload(void) {
    if (font_state.glyphs)       { kfree(font_state.glyphs); font_state.glyphs=NULL; }
    if (font_state.unicode_table){ kfree(font_state.unicode_table); font_state.unicode_table=NULL; }
    font_state.loaded = 0;
}
