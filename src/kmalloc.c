/*  kmalloc.c — First-fit heap allocator for atmkoala OS
 *
 *  Layout of heap:
 *    [block_header_t | user data | padding] [block_header_t | ...] ...
 *
 *  block_header_t is 16 bytes (aligned), storing:
 *    - magic       : 0xC4FE for sanity checks
 *    - size        : usable bytes (not including header)
 *    - free        : 1 = free, 0 = allocated
 *    - next        : pointer to next block header, NULL = last
 */
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

#define HEAP_MAGIC   0xC4FEu
#define ALIGN(n,a)   (((n) + (a) - 1) & ~((a) - 1))
#define MIN_SPLIT    32u   /* don't split if leftover < this */
/* Defensive upper bound: normal ATMKoala heap usage has far fewer blocks.
 * It turns a corrupted next-link cycle into allocation failure, not a ring-0 hang. */
#define HEAP_WALK_MAX 65536u

typedef struct block_hdr {
    uint64_t        magic;
    uint64_t        size;  /* usable bytes */
    uint32_t        free;
    struct block_hdr *next;
} block_header_t;

static block_header_t *heap_start = NULL;
static uint32_t heap_total = 0;

static int heap_header_valid(const block_header_t *hdr){
    uintptr_t start=(uintptr_t)heap_start;
    uintptr_t end=start+(uintptr_t)heap_total+sizeof(block_header_t);
    uintptr_t p=(uintptr_t)hdr;
    return hdr && heap_start && p>=start && p<=end-sizeof(block_header_t) && !(p&7);
}

void heap_init(uintptr_t start_addr, uintptr_t size) {
    /* align start */
    uintptr_t aligned = ALIGN(start_addr, 16);
    if (size < sizeof(block_header_t) + 16) return;
    size -= (aligned - start_addr);

    heap_start = (block_header_t *)aligned;
    heap_start->magic = HEAP_MAGIC;
    heap_start->size  = size - sizeof(block_header_t);
    heap_start->free  = 1;
    heap_start->next  = NULL;
    heap_total        = heap_start->size;
}

/* Merge adjacent free blocks */
static void coalesce(void) {
    block_header_t *cur = heap_start;
    uint32_t seen=0;
    while (cur && cur->next && seen++<HEAP_WALK_MAX) {
        if(!heap_header_valid(cur) || !heap_header_valid(cur->next) || cur->magic != HEAP_MAGIC || cur->next->magic != HEAP_MAGIC) return;
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next  = cur->next->next;
            /* don't advance — check again */
        } else {
            cur = cur->next;
        }
    }
}

void *kmalloc(size_t size) {
    if (!heap_start || size == 0) return NULL;

    /* align size to 8 bytes */
    size = ALIGN(size, 8);

    block_header_t *cur = heap_start;
    uint32_t seen=0;
    while (cur && seen++<HEAP_WALK_MAX) {
        if (!heap_header_valid(cur) || cur->magic != HEAP_MAGIC) return NULL; /* heap corruption */
        if (cur->free && cur->size >= size) {
            /* split block if it's large enough */
            if (cur->size >= size + sizeof(block_header_t) + MIN_SPLIT) {
                block_header_t *newb =
                    (block_header_t *)((uint8_t *)cur
                                       + sizeof(block_header_t) + size);
                newb->magic = HEAP_MAGIC;
                newb->size  = cur->size - size - sizeof(block_header_t);
                newb->free  = 1;
                newb->next  = cur->next;
                cur->next   = newb;
                cur->size   = size;
            }
            cur->free = 0;
            return (void *)((uint8_t *)cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }
    return NULL; /* out of memory */
}

void *kmalloc_aligned(size_t size, size_t align) {
    if (align <= 8) return kmalloc(size);
    /* Simple: over-allocate by align and adjust pointer,
       store real pointer just before the returned address */
    size_t total = size + align + sizeof(void *);
    void *raw = kmalloc(total);
    if (!raw) return NULL;
    uintptr_t addr = (uintptr_t)raw + sizeof(void *);
    addr = ALIGN(addr, align);
    /* store raw pointer before aligned address */
    ((void **)addr)[-1] = raw;
    return (void *)addr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *hdr =
        (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    if (hdr->magic != HEAP_MAGIC) return; /* bad pointer / corruption */
    if (hdr->free) return;                /* double-free guard */
    hdr->free = 1;
    coalesce();
}

uint32_t heap_free_bytes(void) {
    uint32_t total = 0;
    block_header_t *cur = heap_start;
    while (cur) { if (cur->free) total += cur->size; cur = cur->next; }
    return total;
}

uint32_t heap_used_bytes(void) {
    return heap_total - heap_free_bytes();
}
