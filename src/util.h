#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdint.h>

/* String */
size_t  kstrlen(const char *s);
int     kstrcmp(const char *a, const char *b);
int     kstrncmp(const char *a, const char *b, size_t n);
char   *kstrcpy(char *dst, const char *src);
char   *kstrncpy(char *dst, const char *src, size_t n);
char   *kstrcat(char *dst, const char *src);
char   *kstrncat(char *dst, const char *src, size_t n);
char   *kstrchr(const char *s, int c);
char   *kstrrchr(const char *s, int c);
char   *kstrstr(const char *hay, const char *needle);
int     kstrtoi(const char *s);
uint32_t kstrtou(const char *s, int base);

/* Memory */
void   *kmemset(void *ptr, int val, size_t len);
void   *kmemcpy(void *dst, const void *src, size_t n);
void   *kmemmove(void *dst, const void *src, size_t n);
int     kmemcmp(const void *a, const void *b, size_t n);

/* Number formatting */
void    kitoa(int n, char *buf, int base);
void    kuitoa(uint32_t n, char *buf, int base);
int     ksnprintf(char *buf, size_t sz, const char *fmt, ...);

/* Char classification (from POSIX ctype.h) */
static inline int kis_alpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static inline int kis_digit(int c){ return c>='0'&&c<='9'; }
static inline int kis_alnum(int c){ return kis_alpha(c)||kis_digit(c); }
static inline int kis_space(int c){ return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static inline int kis_upper(int c){ return c>='A'&&c<='Z'; }
static inline int kis_lower(int c){ return c>='a'&&c<='z'; }
static inline int kto_upper(int c){ return kis_lower(c)?c-32:c; }
static inline int kto_lower(int c){ return kis_upper(c)?c+32:c; }
static inline int kis_print(int c){ return c>=0x20&&c<0x7F; }
static inline int kis_hex(int c)  { return kis_digit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }

/* I/O ports (Linux asm/io.h style) */
static inline uint8_t  inb(uint16_t p){ uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
static inline uint32_t inl(uint16_t p){ uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p):"memory"); return v; }
/* I/O is also a compiler barrier: PIO transfers must observe the fully
 * prepared sector buffer and complete before the caller reuses it. */
static inline void outb(uint16_t p, uint8_t  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void outl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void io_wait(void){ outb(0x80, 0); }

/* Bit ops (like Linux bitops.h) */
static inline void set_bit(uint32_t *w, int n)  { *w |=  (1u<<n); }
static inline void clr_bit(uint32_t *w, int n)  { *w &= ~(1u<<n); }
static inline int  tst_bit(uint32_t *w, int n)  { return (*w>>n)&1; }

/* CPU */
static inline void cpu_hlt(void)  { __asm__ volatile("hlt"); }
static inline void cpu_cli(void)  { __asm__ volatile("cli"); }
static inline void cpu_sti(void)  { __asm__ volatile("sti"); }
static inline void cpu_nop(void)  { __asm__ volatile("nop"); }
static inline uint64_t cpu_cr0(void){ uint64_t v; __asm__ volatile("mov %%cr0,%0":"=r"(v)); return v; }
static inline uint64_t cpu_cr2(void){ uint64_t v; __asm__ volatile("mov %%cr2,%0":"=r"(v)); return v; }
static inline uint64_t cpu_cr3(void){ uint64_t v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v; }

/* Kernel printf — implemented in util.c, prints via terminal */
void kprintf(const char *fmt, ...);

/* Panic */
__attribute__((noreturn)) void kpanic(const char *msg);

#endif
