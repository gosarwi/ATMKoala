/* ATMKoala native libc allocator — MIT licensed project code.
 * v0.9 grows a process-private brk heap. free() is intentionally a no-op until
 * page reclamation and a reusable free-list policy are introduced. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "atm_native_abi.h"

typedef struct atm_alloc_header {
    size_t size;
} atm_alloc_header_t;

#define ATM_ALLOC_ALIGN 16u
#define ATM_ALLOC_MAX ((size_t)-1 - 64u)

static size_t align_up(size_t n){ return (n+(ATM_ALLOC_ALIGN-1u))&~(ATM_ALLOC_ALIGN-1u); }

void *malloc(size_t size){
    if(!size) size=1;
    if(size>ATM_ALLOC_MAX){errno=ENOMEM;return 0;}
    size_t payload=align_up(size);
    if(payload<size || payload>ATM_ALLOC_MAX-sizeof(atm_alloc_header_t)){errno=ENOMEM;return 0;}
    size_t total=payload+sizeof(atm_alloc_header_t);
    uint64_t current=atm_brk(0);
    if(!current || current>(uint64_t)-1-total){errno=ENOMEM;return 0;}
    uint64_t next=current+total;
    if(atm_brk(next)!=next){errno=ENOMEM;return 0;}
    atm_alloc_header_t *header=(atm_alloc_header_t *)(uintptr_t)current;
    header->size=payload;
    return header+1;
}

void free(void *ptr){ (void)ptr; }

void *calloc(size_t count,size_t size){
    if(size && count>((size_t)-1)/size){errno=ENOMEM;return 0;}
    size_t total=count*size;
    void *ptr=malloc(total);
    if(ptr) memset(ptr,0,total);
    return ptr;
}

void *realloc(void *ptr,size_t size){
    if(!ptr) return malloc(size);
    if(!size){free(ptr);return 0;}
    atm_alloc_header_t *old=((atm_alloc_header_t *)ptr)-1;
    if(size<=old->size) return ptr;
    void *next=malloc(size);
    if(!next) return 0;
    memcpy(next,ptr,old->size);
    return next;
}
