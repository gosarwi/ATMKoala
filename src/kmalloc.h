#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/* Initialize heap starting at given address with given size */
void   heap_init(uintptr_t start_addr, uintptr_t size);

void  *kmalloc(size_t size);
void  *kmalloc_aligned(size_t size, size_t align);
void   kfree(void *ptr);

/* Debug: return total free bytes */
uint32_t heap_free_bytes(void);
uint32_t heap_used_bytes(void);

#endif
