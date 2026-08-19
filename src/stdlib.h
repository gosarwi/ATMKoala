#ifndef ATM_FREESTANDING_STDLIB_H
#define ATM_FREESTANDING_STDLIB_H

/* Compatibility declarations for third-party single-header libraries.
 * ATMKoala's decoder provides STBI_MALLOC/STBI_FREE/STBI_REALLOC_SIZED,
 * so no implementation from a hosted C library is linked. */
#include <stddef.h>
void *malloc(size_t size);
void *realloc(void *ptr,size_t size);
void free(void *ptr);
int abs(int value);
long strtol(const char *nptr,char **endptr,int base);

#endif
