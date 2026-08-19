#ifndef UNICODE_H
#define UNICODE_H
/* unicode.h — atmkoala v0.5 — UTF-8/UTF-16/Unicode utilities */
#include <stdint.h>
#include <stddef.h>

/* Decode one UTF-8 codepoint from *ptr; advance ptr */
static inline uint32_t unicode_decode_utf8(const uint8_t **ptr) {
    const uint8_t *p = *ptr;
    uint32_t cp;
    if      ((*p & 0x80) == 0x00) { cp = *p++;                          }
    else if ((*p & 0xE0) == 0xC0) { cp  = (*p++ & 0x1F) << 6;
                                     cp |= (*p++ & 0x3F);                }
    else if ((*p & 0xF0) == 0xE0) { cp  = (*p++ & 0x0F) << 12;
                                     cp |= (*p++ & 0x3F) << 6;
                                     cp |= (*p++ & 0x3F);                }
    else if ((*p & 0xF8) == 0xF0) { cp  = (*p++ & 0x07) << 18;
                                     cp |= (*p++ & 0x3F) << 12;
                                     cp |= (*p++ & 0x3F) << 6;
                                     cp |= (*p++ & 0x3F);                }
    else                           { cp = 0xFFFD; p++;                   }
    *ptr = p;
    return cp;
}

/* Encode codepoint to UTF-8; returns byte count (1-4), writes to buf */
static inline int unicode_encode_utf8(uint32_t cp, uint8_t *buf) {
    if (cp < 0x80)   { buf[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800)  { buf[0]=0xC0|(cp>>6); buf[1]=0x80|(cp&0x3F); return 2; }
    if (cp < 0x10000){ buf[0]=0xE0|(cp>>12); buf[1]=0x80|((cp>>6)&0x3F);
                       buf[2]=0x80|(cp&0x3F); return 3; }
    buf[0]=0xF0|(cp>>18); buf[1]=0x80|((cp>>12)&0x3F);
    buf[2]=0x80|((cp>>6)&0x3F); buf[3]=0x80|(cp&0x3F); return 4;
}

/* Count codepoints in UTF-8 string */
static inline int unicode_strlen(const char *s) {
    int n = 0; const uint8_t *p = (const uint8_t *)s;
    while (*p) { unicode_decode_utf8(&p); n++; } return n;
}

/* Byte length needed for first n codepoints of s */
static inline int unicode_byte_len(const char *s, int n) {
    const uint8_t *start = (const uint8_t *)s;
    const uint8_t *p     = start;
    for (int i = 0; i < n && *p; i++) unicode_decode_utf8(&p);
    return (int)(p - start);
}

/* Character categories */
static inline int unicode_is_space(uint32_t cp)  { return cp==' '||cp=='\t'||cp==0xA0; }
static inline int unicode_is_lower(uint32_t cp)  { return (cp>='a'&&cp<='z')||(cp>=0x430&&cp<=0x44F)||cp==0x451; }
static inline int unicode_is_upper(uint32_t cp)  { return (cp>='A'&&cp<='Z')||(cp>=0x410&&cp<=0x42F)||cp==0x401; }
static inline int unicode_is_alpha(uint32_t cp)  { return unicode_is_lower(cp)||unicode_is_upper(cp); }
static inline int unicode_is_digit(uint32_t cp)  { return cp>='0'&&cp<='9'; }
static inline int unicode_is_alnum(uint32_t cp)  { return unicode_is_alpha(cp)||unicode_is_digit(cp); }

/* Case conversion */
static inline uint32_t unicode_to_lower(uint32_t cp) {
    if (cp>='A'&&cp<='Z') return cp+32;
    if (cp>=0x410&&cp<=0x42F) return cp+0x20;
    if (cp==0x401) return 0x451;
    return cp;
}
static inline uint32_t unicode_to_upper(uint32_t cp) {
    if (cp>='a'&&cp<='z') return cp-32;
    if (cp>=0x430&&cp<=0x44F) return cp-0x20;
    if (cp==0x451) return 0x401;
    return cp;
}

/* UTF-16LE encode codepoint → 1 or 2 uint16_t; returns count */
static inline int unicode_to_utf16(uint32_t cp, uint16_t *out) {
    if (cp < 0x10000) { out[0]=(uint16_t)cp; return 1; }
    cp -= 0x10000;
    out[0] = (uint16_t)(0xD800 | (cp >> 10));
    out[1] = (uint16_t)(0xDC00 | (cp & 0x3FF));
    return 2;
}

/* Find codepoint in UTF-8 string; returns byte offset or -1 */
static inline int unicode_find(const char *s, uint32_t cp) {
    const uint8_t *p = (const uint8_t *)s, *start = p;
    while (*p) {
        const uint8_t *prev = p;
        if (unicode_decode_utf8(&p) == cp) return (int)(prev - start);
    }
    return -1;
}

/* Simple Cyrillic → Latin transliteration (for VGA fallback) */
static inline char unicode_translit(uint32_t cp) {
    if (cp>='A'&&cp<='z') return (char)cp;
    if (cp>=0x430&&cp<=0x44F) {
        static const char *t[]={"a","b","v","g","d","e","zh","z","i","j","k",
            "l","m","n","o","p","r","s","t","u","f","h","ts","ch","sh","shh",
            "","y","","e","yu","ya"};
        int idx=(int)(cp-0x430);
        return (idx<32&&t[idx][1]==0)?t[idx][0]:'?';
    }
    if (cp>=0x410&&cp<=0x42F) return unicode_translit(cp+0x20);
    if (cp==0x451) return 'e';
    if (cp==0x401) return 'E';
    if (cp<0x80) return (char)cp;
    return '?';
}

#endif
