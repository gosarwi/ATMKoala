/* util.c — atmkoala v0.5 kernel utility library
 * Inspired by Linux lib/string.c and glibc
 */
#include "util.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Forward — terminal_write defined in vga.c */
extern void terminal_write(const char *s);
extern void terminal_putchar(char c);

/* ── String functions ─────────────────────────────────────── */
size_t kstrlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int kstrncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int d = (unsigned char)a[i] - (unsigned char)b[i];
        if (d || !a[i]) return d;
    } return 0;
}
char *kstrcpy(char *dst, const char *src) {
    char *d = dst; while ((*dst++ = *src++)); return d;
}
char *kstrncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
char *kstrcat(char *dst, const char *src) {
    char *d = dst; while (*dst) dst++; while ((*dst++ = *src++)); return d;
}
char *kstrncat(char *dst, const char *src, size_t n) {
    char *d = dst; while (*dst) dst++;
    size_t i = 0; while (i < n && src[i]) *dst++ = src[i++];
    *dst = 0; return d;
}
char *kstrchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == 0) ? (char *)s : NULL;
}
char *kstrrchr(const char *s, int c) {
    const char *last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}
char *kstrstr(const char *hay, const char *needle) {
    size_t nl = kstrlen(needle); if (!nl) return (char *)hay;
    for (; *hay; hay++)
        if (kstrncmp(hay, needle, nl) == 0) return (char *)hay;
    return NULL;
}
int kstrtoi(const char *s) {
    int n = 0, neg = 0;
    while (kis_space(*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (kis_digit(*s)) n = n*10 + (*s++ - '0');
    return neg ? -n : n;
}
uint32_t kstrtou(const char *s, int base) {
    uint32_t n = 0;
    while (kis_space(*s)) s++;
    if (base == 16 && s[0] == '0' && (s[1]=='x'||s[1]=='X')) s += 2;
    const char *d = "0123456789abcdef";
    while (*s) {
        char lc = (char)kto_lower(*s);
        const char *p = kstrchr(d, lc);
        if (!p || (int)(p-d) >= base) break;
        n = n*(uint32_t)base + (uint32_t)(p-d); s++;
    } return n;
}

/* ── Memory functions ─────────────────────────────────────── */
void *kmemset(void *ptr, int val, size_t len) {
    unsigned char *p = ptr;
    while (len--) *p++ = (unsigned char)val;
    return ptr;
}
void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++; return dst;
}
void *kmemmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    while (n--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}

/* ── Number formatting ────────────────────────────────────── */
void kitoa(int n, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0]='0'; buf[1]=0; return; }
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[32]; int i = 0; int neg = (base==10 && n<0);
    if (neg) n = -n;
    unsigned un = (unsigned)n;
    const char *digs = "0123456789abcdef";
    while (un > 0) { tmp[i++] = digs[un % (unsigned)base]; un /= (unsigned)base; }
    if (neg) tmp[i++] = '-';
    int j = 0; while (i--) buf[j++] = tmp[i]; buf[j] = 0;
}
void kuitoa(uint32_t n, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0]='0'; buf[1]=0; return; }
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[32]; int i = 0;
    const char *digs = "0123456789abcdef";
    while (n > 0) { tmp[i++] = digs[n%(uint32_t)base]; n /= (uint32_t)base; }
    int j = 0; while (i--) buf[j++] = tmp[i]; buf[j] = 0;
}
void ku64toa(uint64_t n, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0]='0'; buf[1]=0; return; }
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[65]; int i = 0;
    const char *digs = "0123456789abcdef";
    while (n > 0) { tmp[i++] = digs[n%(uint64_t)base]; n /= (uint64_t)base; }
    int j = 0; while (i--) buf[j++] = tmp[i]; buf[j] = 0;
}

/* ── ksnprintf — safe formatted print to buffer ───────────── */
int ksnprintf(char *buf, size_t sz, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    size_t pos = 0;
    char nb[32];
#define PUT(c) do { if (pos+1 < sz) buf[pos++] = (c); } while(0)
    while (*fmt && pos+1 < sz) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        /* Width */
        int width = 0, zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (kis_digit(*fmt)) { width = width*10 + (*fmt++ - '0'); }
        /* Specifier */
        if (*fmt == 'd') {
            int n = va_arg(ap, int); kitoa(n, nb, 10);
            size_t l = kstrlen(nb);
            char pad = zero_pad ? '0' : ' ';
            for (size_t i = l; (int)i < width; i++) PUT(pad);
            for (size_t i = 0; nb[i]; i++) PUT(nb[i]);
        } else if (*fmt == 'u') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 10);
            size_t l = kstrlen(nb);
            char pad = zero_pad ? '0' : ' ';
            for (size_t i = l; (int)i < width; i++) PUT(pad);
            for (size_t i = 0; nb[i]; i++) PUT(nb[i]);
        } else if (*fmt == 'x' || *fmt == 'X') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 16);
            if (*fmt == 'X') { for (int i=0;nb[i];i++) nb[i]=(char)kto_upper(nb[i]); }
            size_t l = kstrlen(nb);
            for (size_t i = l; (int)i < width; i++) PUT(zero_pad?'0':' ');
            for (size_t i = 0; nb[i]; i++) PUT(nb[i]);
        } else if (*fmt == 'p') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 16);
            PUT('0'); PUT('x');
            for (size_t i = 0; nb[i]; i++) PUT(nb[i]);
        } else if (*fmt == 's') {
            const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
            while (*s) PUT(*s++);
        } else if (*fmt == 'c') {
            PUT((char)va_arg(ap, int));
        } else if (*fmt == '%') {
            PUT('%');
        }
        fmt++;
    }
#undef PUT
    buf[pos] = 0;
    va_end(ap);
    return (int)pos;
}

/* ── kprintf — print to terminal ─────────────────────────── */
void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char nb[32];
    while (*fmt) {
        if (*fmt != '%') { terminal_putchar(*fmt++); continue; }
        fmt++;
        int width = 0, zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (kis_digit(*fmt)) { width = width*10 + (*fmt++ - '0'); }
        if (*fmt == 'd') {
            int n = va_arg(ap, int); kitoa(n, nb, 10);
            size_t l = kstrlen(nb); char pad = zero_pad?'0':' ';
            for (size_t i=l;(int)i<width;i++) terminal_putchar(pad);
            terminal_write(nb);
        } else if (*fmt == 'u') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 10);
            size_t l = kstrlen(nb); char pad = zero_pad?'0':' ';
            for (size_t i=l;(int)i<width;i++) terminal_putchar(pad);
            terminal_write(nb);
        } else if (*fmt == 'x') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 16);
            size_t l = kstrlen(nb);
            for (size_t i=l;(int)i<width;i++) terminal_putchar(zero_pad?'0':' ');
            terminal_write(nb);
        } else if (*fmt == 'p') {
            uint32_t n = va_arg(ap, uint32_t); kuitoa(n, nb, 16);
            terminal_write("0x"); terminal_write(nb);
        } else if (*fmt == 's') {
            const char *s = va_arg(ap, const char*);
            terminal_write(s ? s : "(null)");
        } else if (*fmt == 'c') {
            terminal_putchar((char)va_arg(ap, int));
        } else if (*fmt == '%') {
            terminal_putchar('%');
        }
        fmt++;
    }
    va_end(ap);
}

/* ── kpanic ───────────────────────────────────────────────── */
__attribute__((noreturn)) void kpanic(const char *msg) {
    cpu_cli();
    terminal_write("\n\n*** KERNEL PANIC: ");
    terminal_write(msg);
    terminal_write(" ***\n");
    while (1) cpu_hlt();
}

/* ── Стандартные имена для линкера (gcc вставляет их автоматически) ── */
void *memcpy(void *dst, const void *src, size_t n)
    __attribute__((alias("kmemcpy")));

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
    __attribute__((alias("kmemset")));

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}
