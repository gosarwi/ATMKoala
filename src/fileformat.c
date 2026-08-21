/* fileformat.c — File format detection & display for atmkoala v0.5 */
#include "fileformat.h"
#include "vga.h"
#include "vbe.h"
#include "util.h"
#include "mp3.h"
#include <stdint.h>
#include <stddef.h>

/* ── Format detection ─────────────────────────────────────── */
static const char *ext_of(const char *name) {
    if (!name) return "";
    const char *dot = name, *p = name;
    while (*p) { if (*p == '.') dot = p; p++; }
    return (dot == name) ? "" : dot+1;
}

file_fmt_t fmt_detect(const uint8_t *buf, uint32_t size, const char *fn) {
    /* Magic bytes first */
    if (size >= 4) {
        /* ELF */
        if (buf[0]==0x7F && buf[1]=='E' && buf[2]=='L' && buf[3]=='F') return FMT_ELF32;
        /* Zstandard frame: package payload is a USTAR archive. */
        uint32_t magic = buf[0]|(buf[1]<<8)|(buf[2]<<16)|((uint32_t)buf[3]<<24);
        if (magic == 0xFD2FB528u) return FMT_TAR_ZST;
        /* BMP */
        if (buf[0]=='B' && buf[1]=='M') return FMT_BMP;
        /* PNG signature */
        if (size>=8 && buf[0]==0x89 && buf[1]=='P' && buf[2]=='N' && buf[3]=='G' && buf[4]==0x0D && buf[5]==0x0A && buf[6]==0x1A && buf[7]==0x0A) return FMT_PNG;
        /* JPEG SOI */
        if (buf[0]==0xFF && buf[1]==0xD8 && buf[2]==0xFF) return FMT_JPEG;
        /* GZIP */
        if (buf[0]==0x1F && buf[1]==0x8B) return FMT_GZIP;
        /* MPEG Layer III: require a bounded multi-frame probe, not a filename. */
        if (atm_mp3_probe(buf,size,&(atm_mp3_info_t){0})==0) return FMT_MP3;
        /* TAR (check at offset 257) */
        if (size > 262 && buf[257]=='u' && buf[258]=='s' && buf[259]=='t' &&
            buf[260]=='a' && buf[261]=='r') return FMT_TAR;
    }

    /* Extension-based */
    const char *e = ext_of(fn);
    if (!kstrcmp(e,"txt") || !kstrcmp(e,"log") || !kstrcmp(e,"text")) return FMT_TEXT;
    if (!kstrcmp(e,"md")  || !kstrcmp(e,"markdown"))                   return FMT_MARKDOWN;
    if (!kstrcmp(e,"c")   || !kstrcmp(e,"h") || !kstrcmp(e,"cpp"))    return FMT_C_SOURCE;
    if (!kstrcmp(e,"py"))  return FMT_PYTHON;
    if (!kstrcmp(e,"sh"))  return FMT_SHELL;
    if (!kstrcmp(e,"json")) return FMT_JSON;
    if (!kstrcmp(e,"toml")) return FMT_TOML;
    if (!kstrcmp(e,"ini") || !kstrcmp(e,"conf") || !kstrcmp(e,"cfg")) return FMT_INI;
    if (!kstrcmp(e,"csv")) return FMT_CSV;
    if (!kstrcmp(e,"zst") || !kstrcmp(e,"tzst") || !kstrcmp(e,"atpk")) return FMT_TAR_ZST;
    if (!kstrcmp(e,"tar"))  return FMT_TAR;
    if (!kstrcmp(e,"gz"))   return FMT_GZIP;
    if (!kstrcmp(e,"bmp"))  return FMT_BMP;
    if (!kstrcmp(e,"png"))  return FMT_PNG;
    if (!kstrcmp(e,"jpg") || !kstrcmp(e,"jpeg")) return FMT_JPEG;
    if (!kstrcmp(e,"mp3")) return FMT_MP3;

    return FMT_TEXT;  /* default: try as text */
}

const char *fmt_name(file_fmt_t f) {
    switch(f) {
        case FMT_TEXT:     return "Plain Text";
        case FMT_MARKDOWN: return "Markdown";
        case FMT_C_SOURCE: return "C/C++ Source";
        case FMT_PYTHON:   return "Python Script";
        case FMT_SHELL:    return "Shell Script";
        case FMT_JSON:     return "JSON";
        case FMT_TOML:     return "TOML";
        case FMT_INI:      return "INI/Config";
        case FMT_CSV:      return "CSV";
        case FMT_ELF32:    return "ELF32 Binary";
        case FMT_TAR_ZST:  return "TAR.ZST Package";
        case FMT_BMP:      return "BMP Image";
        case FMT_PNG:      return "PNG Image";
        case FMT_JPEG:     return "JPEG Image";
        case FMT_TAR:      return "TAR Archive";
        case FMT_GZIP:     return "GZip Archive";
        case FMT_MP3:      return "MPEG Audio Layer III (inspection only)";
        default:           return "Unknown";
    }
}

const char *fmt_mime(file_fmt_t f) {
    switch(f) {
        case FMT_TEXT:     return "text/plain";
        case FMT_MARKDOWN: return "text/markdown";
        case FMT_C_SOURCE: return "text/x-csrc";
        case FMT_JSON:     return "application/json";
        case FMT_CSV:      return "text/csv";
        case FMT_ELF32:    return "application/x-elf";
        case FMT_BMP:      return "image/bmp";
        case FMT_PNG:      return "image/png";
        case FMT_JPEG:     return "image/jpeg";
        case FMT_TAR_ZST:  return "application/zstd";
        case FMT_TAR:      return "application/x-tar";
        case FMT_GZIP:     return "application/gzip";
        case FMT_MP3:      return "audio/mpeg";
        default:           return "application/octet-stream";
    }
}

int fmt_mp3_info(const uint8_t *buf,uint32_t size,void *info_out){return atm_mp3_probe(buf,size,(atm_mp3_info_t *)info_out);}

int fmt_is_text(file_fmt_t f) {
    return (f==FMT_TEXT||f==FMT_MARKDOWN||f==FMT_C_SOURCE||
            f==FMT_PYTHON||f==FMT_SHELL||f==FMT_JSON||
            f==FMT_TOML||f==FMT_INI||f==FMT_CSV);
}

/* ── wc ───────────────────────────────────────────────────── */
wc_result_t fmt_wc(const char *buf, uint32_t size) {
    wc_result_t r = {0,0,0,(int)size};
    int in_word = 0;
    for (uint32_t i = 0; i < size; i++) {
        r.chars++;
        if (buf[i] == '\n') r.lines++;
        if (buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'||buf[i]=='\r') in_word=0;
        else if (!in_word) { r.words++; in_word=1; }
    }
    return r;
}

/* ── Permissions string ───────────────────────────────────── */
void fmt_perm_str(uint8_t perms, int is_dir, char out[11]) {
    out[0] = is_dir ? 'd' : '-';
    out[1] = (perms & 0x4) ? 'r' : '-';
    out[2] = (perms & 0x2) ? 'w' : '-';
    out[3] = (perms & 0x1) ? 'x' : '-';
    out[4] = (perms & 0x4) ? 'r' : '-';
    out[5] = (perms & 0x2) ? 'w' : '-';
    out[6] = '-';
    out[7] = (perms & 0x4) ? 'r' : '-';
    out[8] = '-'; out[9] = '-'; out[10] = 0;
}

/* ── Syntax-highlighted text printing ─────────────────────── */
/* VGA color codes for syntax */
#define SYN_KEYWORD  VGA_LIGHT_CYAN
#define SYN_STRING   VGA_LIGHT_GREEN
#define SYN_COMMENT  VGA_DARK_GREY
#define SYN_NUMBER   VGA_LIGHT_MAGENTA
#define SYN_SECTION  VGA_YELLOW
#define SYN_KEY      VGA_LIGHT_CYAN
#define SYN_VALUE    VGA_LIGHT_GREY
#define SYN_HEADER   VGA_YELLOW
#define SYN_BOLD     VGA_WHITE
#define SYN_NORMAL   VGA_LIGHT_GREY
#define SYN_BG       VGA_BLACK

static int is_c_keyword(const char *word, int len) {
    static const char *kw[] = {
        "int","char","void","return","if","else","for","while","do","break",
        "continue","struct","typedef","static","const","unsigned","uint32_t",
        "uint8_t","uint16_t","size_t","include","define","endif","ifndef",
        "ifdef","extern","inline","enum","switch","case","default","NULL",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        int kl = (int)kstrlen(kw[i]);
        if (kl == len && kstrncmp(word, kw[i], (size_t)len) == 0) return 1;
    }
    return 0;
}

static int is_py_keyword(const char *word, int len) {
    static const char *kw[] = {
        "def","class","if","else","elif","for","while","return","import",
        "from","as","in","not","and","or","True","False","None","pass",
        "break","continue","with","try","except","finally","raise","lambda",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        int kl = (int)kstrlen(kw[i]);
        if (kl == len && kstrncmp(word, kw[i], (size_t)len) == 0) return 1;
    }
    return 0;
}

void fmt_print_text(const char *buf, uint32_t size, file_fmt_t fmt) {
    switch(fmt) {
        case FMT_JSON:     fmt_print_json(buf, size); return;
        case FMT_MARKDOWN: fmt_print_markdown(buf, size); return;
        case FMT_CSV:      fmt_print_csv(buf, size); return;
        default: break;
    }

    const color_scheme_t *s = terminal_current_scheme();
    int lineno = 1;
    const char *p = buf;
    const char *end = buf + size;
    int show_lineno = (fmt==FMT_C_SOURCE||fmt==FMT_PYTHON||fmt==FMT_SHELL);

    while (p < end) {
        /* Line number */
        if (show_lineno) {
            char lnbuf[8]; kitoa(lineno, lnbuf, 10);
            terminal_set_color(SYN_COMMENT, SYN_BG);
            int pad = 4 - (int)kstrlen(lnbuf);
            for (int i=0;i<pad;i++) terminal_putchar(' ');
            terminal_write(lnbuf);
            terminal_write(" | ");
        }
        terminal_set_color(SYN_NORMAL, SYN_BG);

        /* Process one line */
        while (p < end && *p != '\n') {
            /* Comments */
            if ((fmt==FMT_C_SOURCE||fmt==FMT_SHELL) &&
                *p=='/' && *(p+1)=='/') {
                terminal_set_color(SYN_COMMENT, SYN_BG);
                while (p < end && *p != '\n') terminal_putchar(*p++);
                break;
            }
            if (fmt==FMT_SHELL && *p=='#') {
                terminal_set_color(SYN_COMMENT, SYN_BG);
                while (p < end && *p != '\n') terminal_putchar(*p++);
                break;
            }
            if (fmt==FMT_C_SOURCE && *p=='#') {
                terminal_set_color(VGA_LIGHT_MAGENTA, SYN_BG);
                while (p < end && *p != '\n') terminal_putchar(*p++);
                break;
            }
            /* INI: sections and keys */
            if (fmt==FMT_INI) {
                if (*p == '[') {
                    terminal_set_color(SYN_SECTION, SYN_BG);
                    while (p < end && *p != '\n') terminal_putchar(*p++);
                    break;
                }
                if (*p == '#' || *p == ';') {
                    terminal_set_color(SYN_COMMENT, SYN_BG);
                    while (p < end && *p != '\n') terminal_putchar(*p++);
                    break;
                }
                /* key=value: key in cyan, value in white */
                const char *eq = p;
                while (eq < end && *eq != '=' && *eq != '\n') eq++;
                if (*eq == '=') {
                    terminal_set_color(SYN_KEY, SYN_BG);
                    while (p < eq) terminal_putchar(*p++);
                    terminal_set_color(SYN_COMMENT, SYN_BG);
                    terminal_putchar(*p++); /* = */
                    terminal_set_color(SYN_VALUE, SYN_BG);
                    while (p < end && *p != '\n') terminal_putchar(*p++);
                    break;
                }
            }
            /* Strings */
            if (fmt==FMT_C_SOURCE && (*p=='"' || *p=='\'')) {
                char q = *p;
                terminal_set_color(SYN_STRING, SYN_BG);
                terminal_putchar(*p++);
                while (p < end && *p != '\n' && *p != q) {
                    if (*p == '\\') { terminal_putchar(*p++); }
                    terminal_putchar(*p++);
                }
                if (*p == q) terminal_putchar(*p++);
                terminal_set_color(SYN_NORMAL, SYN_BG);
                continue;
            }
            /* Numbers */
            if (*p >= '0' && *p <= '9') {
                terminal_set_color(SYN_NUMBER, SYN_BG);
                while (p < end && ((*p>='0'&&*p<='9') || *p=='x'||*p=='.'||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')))
                    terminal_putchar(*p++);
                terminal_set_color(SYN_NORMAL, SYN_BG);
                continue;
            }
            /* Keywords */
            if ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_') {
                const char *word = p;
                while (p < end && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))
                    p++;
                int wlen = (int)(p - word);
                int kw = (fmt==FMT_C_SOURCE) ? is_c_keyword(word, wlen)
                       : (fmt==FMT_PYTHON)   ? is_py_keyword(word, wlen)
                       : 0;
                if (kw) terminal_set_color(SYN_KEYWORD, SYN_BG);
                else    terminal_set_color(SYN_NORMAL, SYN_BG);
                for (int i=0;i<wlen;i++) terminal_putchar(word[i]);
                continue;
            }
            terminal_putchar(*p++);
        }
        terminal_putchar('\n');
        if (p < end && *p == '\n') p++;
        lineno++;
        terminal_set_color(s->normal_fg, s->normal_bg);
    }
}

void fmt_print_json(const char *buf, uint32_t size) {
    const char *p = buf, *end = buf + size;
    while (p < end) {
        if (*p == '"') {
            terminal_set_color(SYN_STRING, SYN_BG);
            terminal_putchar(*p++);
            while (p<end && *p!='"') { if(*p=='\\')terminal_putchar(*p++); terminal_putchar(*p++); }
            if (p<end) terminal_putchar(*p++);
            terminal_set_color(SYN_NORMAL, SYN_BG);
        } else if (*p=='{' || *p=='}' || *p=='[' || *p==']') {
            terminal_set_color(SYN_SECTION, SYN_BG); terminal_putchar(*p++);
            terminal_set_color(SYN_NORMAL, SYN_BG);
        } else if (*p==':') {
            terminal_set_color(SYN_COMMENT, SYN_BG); terminal_putchar(*p++);
            terminal_set_color(SYN_NORMAL, SYN_BG);
        } else if (*p>='0' && *p<='9') {
            terminal_set_color(SYN_NUMBER, SYN_BG);
            while (p<end && (*p>='0'&&*p<='9' || *p=='.'|| *p=='-')) terminal_putchar(*p++);
            terminal_set_color(SYN_NORMAL, SYN_BG);
        } else { terminal_putchar(*p++); }
    }
}

void fmt_print_markdown(const char *buf, uint32_t size) {
    const char *p = buf, *end = buf + size;
    const color_scheme_t *s = terminal_current_scheme();
    while (p < end) {
        if (*p == '#') {
            int lvl = 0; while (*p == '#') { lvl++; p++; }
            terminal_set_color(lvl==1 ? VGA_YELLOW :
                               lvl==2 ? VGA_LIGHT_CYAN : VGA_WHITE, SYN_BG);
            while (p<end && *p!='\n') terminal_putchar(*p++);
            terminal_putchar('\n'); if (p<end) p++;
            terminal_set_color(s->normal_fg, s->normal_bg);
        } else if (*p=='*' && *(p+1)=='*') {
            p+=2; terminal_set_color(SYN_BOLD, SYN_BG);
            while (p<end && !(*p=='*'&&*(p+1)=='*')) terminal_putchar(*p++);
            if (p<end) p+=2;
            terminal_set_color(s->normal_fg, s->normal_bg);
        } else if (*p=='`') {
            terminal_set_color(SYN_STRING, SYN_BG);
            while (p<end && *p!='\n') terminal_putchar(*p++);
            terminal_set_color(s->normal_fg, s->normal_bg);
        } else if (*p=='-' || *p=='*') {
            terminal_set_color(SYN_SECTION, SYN_BG); terminal_putchar(*p++);
            terminal_set_color(s->normal_fg, s->normal_bg);
            while (p<end && *p!='\n') terminal_putchar(*p++);
            terminal_putchar('\n'); if (p<end) p++;
        } else {
            terminal_putchar(*p++);
        }
    }
}

void fmt_print_csv(const char *buf, uint32_t size) {
    const char *p = buf, *end = buf + size;
    int row = 0;
    const color_scheme_t *s = terminal_current_scheme();
    while (p < end) {
        int col = 0;
        if (row == 0) terminal_set_color(SYN_KEYWORD, SYN_BG); /* header row */
        else terminal_set_color(s->normal_fg, s->normal_bg);
        while (p < end && *p != '\n') {
            if (*p == ',') {
                terminal_set_color(SYN_COMMENT, SYN_BG);
                terminal_putchar('|');
                terminal_set_color(row==0 ? SYN_KEYWORD : s->normal_fg, s->normal_bg);
                col++; p++;
            } else terminal_putchar(*p++);
        }
        terminal_putchar('\n');
        if (p < end) p++;
        row++;
        (void)col;
    }
    terminal_set_color(s->normal_fg, s->normal_bg);
}

/* ── BMP viewer ───────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t sig;        /* 'BM' */
    uint32_t size;
    uint16_t res1, res2;
    uint32_t offset;     /* pixel data offset */
} bmp_file_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t hdr_size;
    int32_t  width, height;
    uint16_t planes, bpp;
    uint32_t compression;
    uint32_t img_size;
    int32_t  xppm, yppm;
    uint32_t clr_used, clr_imp;
} bmp_info_hdr_t;

int fmt_bmp_info(const uint8_t *buf, uint32_t size,
                 int *width, int *height, int *bpp) {
    if (size < sizeof(bmp_file_hdr_t)+sizeof(bmp_info_hdr_t)) return -1;
    const bmp_file_hdr_t *fh = (const bmp_file_hdr_t *)buf;
    const bmp_info_hdr_t *ih = (const bmp_info_hdr_t *)(buf + sizeof(bmp_file_hdr_t));
    if (fh->sig != 0x4D42) return -1;  /* 'BM' */
    *width  = ih->width;
    *height = (ih->height < 0) ? -ih->height : ih->height;
    *bpp    = ih->bpp;
    return 0;
}

int fmt_bmp_draw(const uint8_t *buf, uint32_t size, int x, int y) {
    if (!vbe.active) return -1;
    const bmp_file_hdr_t *fh = (const bmp_file_hdr_t *)buf;
    const bmp_info_hdr_t *ih = (const bmp_info_hdr_t *)(buf + sizeof(bmp_file_hdr_t));
    if (size < fh->offset) return -1;

    int w = ih->width;
    int h = ih->height < 0 ? -ih->height : ih->height;
    int bpp = ih->bpp;
    int inverted = (ih->height > 0);  /* BMP is usually bottom-up */
    int row_bytes = ((w * bpp/8 + 3) & ~3);

    const uint8_t *pixels = buf + fh->offset;

    for (int py = 0; py < h; py++) {
        int src_row = inverted ? (h-1-py) : py;
        const uint8_t *row = pixels + src_row * row_bytes;
        for (int px = 0; px < w; px++) {
            color32_t c;
            if (bpp == 24) {
                uint8_t b=row[px*3], g=row[px*3+1], r=row[px*3+2];
                c = RGB(r,g,b);
            } else if (bpp == 32) {
                uint8_t b=row[px*4], g=row[px*4+1], r=row[px*4+2];
                c = RGB(r,g,b);
            } else continue;
            vbe_putpixel(x+px, y+py, c);
        }
    }
    return 0;
}

/* ── TAR listing ──────────────────────────────────────────── */
int fmt_tar_list(const uint8_t *buf, uint32_t size) {
    const char *s = terminal_current_scheme()->name;
    (void)s;
    uint32_t off = 0;
    int count = 0;
    terminal_writeln("TAR archive contents:");
    terminal_writeln("  Mode       Size      Name");
    terminal_writeln("  ---------  --------  ----");
    while (off + 512 <= size) {
        const char *name = (const char *)(buf + off);
        if (name[0] == 0) break;  /* end of archive */
        /* Size is octal at offset 124 */
        uint32_t sz = 0;
        const char *so = (const char *)(buf + off + 124);
        while (*so >= '0' && *so <= '7') sz = sz*8 + (uint32_t)(*so++ - '0');
        /* Mode at offset 100 */
        char mode[12]; kmemset(mode, '-', 10); mode[10]=0;
        const char *mo = (const char *)(buf + off + 100);
        uint32_t mval = 0;
        while (*mo >= '0' && *mo <= '7') mval = mval*8 + (uint32_t)(*mo++ - '0');
        if (mval & 0400) mode[1]='r'; if (mval & 0200) mode[2]='w';
        if (mval & 0100) mode[3]='x'; if (mval & 0040) mode[4]='r';
        if (mval & 0020) mode[5]='w'; if (mval & 0010) mode[6]='x';
        if (mval & 0004) mode[7]='r'; if (mval & 0002) mode[8]='w';
        if (mval & 0001) mode[9]='x';
        char szbuf[12]; kuitoa(sz, szbuf, 10);
        terminal_write("  "); terminal_write(mode);
        terminal_write("  "); int sl=(int)kstrlen(szbuf);
        for(int i=sl;i<8;i++) terminal_putchar(' ');
        terminal_write(szbuf);
        terminal_write("  "); terminal_writeln(name);
        off += 512 + ((sz + 511) & ~511u);
        count++;
    }
    char cb[8]; kuitoa((uint32_t)count, cb, 10);
    terminal_write("  Total: "); terminal_write(cb); terminal_writeln(" files");
    return count;
}

/* ── JSON parser ──────────────────────────────────────────── */
int json_get_string(const char *json, const char *key,
                    char *out, int outsz) {
    /* Search for "key": "value" */
    size_t klen = kstrlen(key);
    const char *p = json;
    while (*p) {
        if (*p == '"') {
            p++;
            if (kstrncmp(p, key, klen) == 0 && p[klen] == '"') {
                p += klen + 1;
                while (*p && (*p==' '||*p=='\t'||*p==':')) p++;
                if (*p == '"') {
                    p++;
                    int i=0;
                    while (*p && *p != '"' && i < outsz-1) out[i++]=*p++;
                    out[i]=0;
                    return i;
                }
            }
        }
        p++;
    }
    return -1;
}

int json_get_int(const char *json, const char *key, int *out) {
    char buf[32];
    if (json_get_string(json, key, buf, sizeof(buf)) < 0) return -1;
    int v = 0, neg = 0;
    const char *p = buf;
    if (*p == '-') { neg=1; p++; }
    while (*p >= '0' && *p <= '9') v = v*10 + (*p++ - '0');
    *out = neg ? -v : v;
    return 0;
}

/* ── TOML parser ──────────────────────────────────────────── */
int toml_get(const char *toml, const char *section,
             const char *key, char *out, int outsz) {
    /* Find [section] then key = value */
    char sec_hdr[128]; kstrcpy(sec_hdr, "["); kstrcat(sec_hdr, section); kstrcat(sec_hdr, "]");
    const char *p = toml;
    int in_section = (section[0] == 0);  /* empty section = global */

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '[') {
            /* Check if this is our section */
            in_section = (kstrncmp(p, sec_hdr, kstrlen(sec_hdr)) == 0);
        } else if (in_section && *p != '#' && *p != '\n' && *p != '\r') {
            size_t klen = kstrlen(key);
            if (kstrncmp(p, key, klen) == 0) {
                const char *eq = p + klen;
                while (*eq == ' ' || *eq == '\t') eq++;
                if (*eq == '=') {
                    eq++;
                    while (*eq == ' ' || *eq == '\t') eq++;
                    /* Strip quotes if present */
                    if (*eq == '"' || *eq == '\'') eq++;
                    int i = 0;
                    while (*eq && *eq != '\n' && *eq != '\r' &&
                           *eq != '"' && *eq != '\'' && i < outsz-1)
                        out[i++] = *eq++;
                    /* trim trailing space */
                    while (i > 0 && (out[i-1]==' '||out[i-1]=='\t')) i--;
                    out[i] = 0;
                    return i;
                }
            }
        }
        /* Advance to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return -1;
}
