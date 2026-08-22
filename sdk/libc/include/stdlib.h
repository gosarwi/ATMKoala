#ifndef ATM_LIBC_STDLIB_H
#define ATM_LIBC_STDLIB_H

#include <stddef.h>

/* x86-64 ATM ABI uses 64-bit long. */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

void *malloc(size_t size);
void *calloc(size_t count,size_t size);
void *realloc(void *ptr,size_t size);
void  free(void *ptr);
void  exit(int status) __attribute__((noreturn));

/* Environment entries are supplied by bounded native execve startup. The
 * current static runtime exposes read-only lookup; setenv/putenv are absent. */
extern char **environ;
char *getenv(const char *name);

int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numer,int denom);
ldiv_t ldiv(long numer,long denom);
lldiv_t lldiv(long long numer,long long denom);

int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *restrict s,char **restrict end,int base);
unsigned long strtoul(const char *restrict s,char **restrict end,int base);
long long strtoll(const char *restrict s,char **restrict end,int base);
unsigned long long strtoull(const char *restrict s,char **restrict end,int base);

void *bsearch(const void *key,const void *base,size_t nel,size_t width,int (*cmp)(const void *,const void *));

#endif
