#ifndef FILEFORMAT_H
#define FILEFORMAT_H

/*  fileformat.h — File format detection and handling for atmkoala v0.5
 *
 *  Supported formats:
 *    Detection:  any file (magic bytes + extension)
 *    Text/view:  .txt .md .conf .ini .toml .sh .c .h .py .json .csv .log
 *    Binary:     .elf (parsed structures)
 *    Image:      .bmp .png .jpg .jpeg (display on VBE)
 *    Config:     .conf .ini .toml (parsed as key-value)
 *    Archive:    .tar .tar.zst (list/package contents)
 */
#include <stdint.h>
#include <stddef.h>

typedef enum {
    FMT_UNKNOWN  = 0,
    FMT_TEXT,          /* plain text */
    FMT_MARKDOWN,      /* .md */
    FMT_C_SOURCE,      /* .c .h */
    FMT_PYTHON,        /* .py */
    FMT_SHELL,         /* .sh */
    FMT_JSON,          /* .json */
    FMT_TOML,          /* .toml */
    FMT_INI,           /* .ini .conf */
    FMT_CSV,           /* .csv */
    FMT_ELF32,         /* ELF32 binary */
    FMT_TAR_ZST,       /* Zstandard-framed USTAR package */
    FMT_BMP,           /* BMP image */
    FMT_PNG,           /* PNG image */
    FMT_JPEG,          /* JPEG image */
    FMT_PPM,           /* binary PPM P6 image */
    FMT_TAR,           /* TAR archive */
    FMT_GZIP,          /* .gz */
    FMT_MP3,           /* MPEG Audio Layer III stream */
} file_fmt_t;

typedef struct {
    file_fmt_t  fmt;
    const char *name;       /* human-readable name */
    const char *mime;       /* MIME type */
    int         is_text;
    int         is_binary;
    int          is_image;
    int          is_audio;

} fmt_info_t;

/* Detect format from buffer (magic bytes) and/or filename extension */
file_fmt_t   fmt_detect(const uint8_t *buf, uint32_t size, const char *filename);
const char  *fmt_name(file_fmt_t fmt);
const char  *fmt_mime(file_fmt_t fmt);
int          fmt_is_text(file_fmt_t fmt);

/* ── Text rendering with syntax highlight (VGA) ── */
void fmt_print_text(const char *buf, uint32_t size, file_fmt_t fmt);
void fmt_print_json(const char *buf, uint32_t size);
void fmt_print_markdown(const char *buf, uint32_t size);
void fmt_print_csv(const char *buf, uint32_t size);

/* ── BMP viewer (VBE only) ── */
int  fmt_bmp_info(const uint8_t *buf, uint32_t size,
                  int *width, int *height, int *bpp);
int  fmt_bmp_draw(const uint8_t *buf, uint32_t size, int x, int y);

/* MP3 stream inspection only; no playback is implied by this function. */
int  fmt_mp3_info(const uint8_t *buf,uint32_t size,void *info_out);

/* ── TAR listing ── */
int  fmt_tar_list(const uint8_t *buf, uint32_t size);

/* ── JSON minimal parser ── */
/* Returns value for key in a flat JSON object (no nesting) */
int  json_get_string(const char *json, const char *key,
                     char *out, int outsz);
int  json_get_int(const char *json, const char *key, int *out);

/* ── TOML minimal parser (flat, one level) ── */
int  toml_get(const char *toml, const char *section,
              const char *key, char *out, int outsz);

/* ── wc equivalent ── */
typedef struct { int lines; int words; int chars; int bytes; } wc_result_t;
wc_result_t  fmt_wc(const char *buf, uint32_t size);

/* ── file permissions string (like ls -l) ── */
void fmt_perm_str(uint8_t perms, int is_dir, char out[11]);

#endif
