#include "paging.h"
#include "kmalloc.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

static uintptr_t g_kernel_cr3=0;

static uint64_t *page_alloc(void){
    uint64_t *p=(uint64_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    if(p) kmemset(p,0,ATM_PAGE_SIZE);
    return p;
}
static uint64_t *phys_ptr(uint64_t entry){
    return (uint64_t *)(uintptr_t)(entry & ATM_PAGE_MASK);
}
static int user_window(uint64_t va){
    return va>=ATM_USER_BASE && va<ATM_USER_TOP;
}

void paging_init(void){
    g_kernel_cr3=(uintptr_t)(cpu_cr3() & ATM_PAGE_MASK);
}
uintptr_t paging_kernel_cr3(void){ return g_kernel_cr3; }

int paging_create_user_space(user_space_t *s){
    if(!s) return -1;
    kmemset(s,0,sizeof(*s));
    if(!g_kernel_cr3) paging_init();
    uint64_t *kpml4=(uint64_t *)(uintptr_t)g_kernel_cr3;
    uint64_t *kpdpt=phys_ptr(kpml4[0]);
    if(!(kpml4[0]&ATM_PTE_P) || !kpdpt) return -1;

    s->pml4=page_alloc(); s->pdpt=page_alloc(); s->user_pt=page_alloc();
    if(!s->pml4||!s->pdpt||!s->user_pt) return -1;
    for(int i=0;i<4;i++){
        s->pd[i]=page_alloc();
        if(!s->pd[i]) return -1;
    }

    kmemcpy(s->pml4,kpml4,ATM_PAGE_SIZE);
    kmemcpy(s->pdpt,kpdpt,ATM_PAGE_SIZE);
    for(int i=0;i<4;i++){
        uint64_t e=kpdpt[i];
        if(!(e&ATM_PTE_P)) return -1;
        uint64_t *kpd=phys_ptr(e);
        kmemcpy(s->pd[i],kpd,ATM_PAGE_SIZE);
        s->pdpt[i]=((uint64_t)(uintptr_t)s->pd[i]&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W;
    }

    /* 0x40000000 lies in PML4[0], PDPT[1], PD[0].  Only this 2 MiB
     * window is split into 4 KiB pages and made user-accessible. */
    s->pml4[0]=((uint64_t)(uintptr_t)s->pdpt&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    s->pdpt[1]=((uint64_t)(uintptr_t)s->pd[1]&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    s->pd[1][0]=((uint64_t)(uintptr_t)s->user_pt&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    s->cr3=(uintptr_t)s->pml4;
    s->valid=1;
    return 0;
}

int paging_map_user_page(user_space_t *s,uint64_t va,uintptr_t phys,uint64_t flags){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    uint64_t pi=(va>>12)&0x1ffULL;
    s->user_pt[pi]=((uint64_t)phys&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_U|(flags&ATM_PTE_W)|(flags&ATM_PTE_NX);
    if(pi>=s->mapped_pages) s->mapped_pages=pi+1;
    return 0;
}
int paging_unmap_user_page(user_space_t *s,uint64_t va){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    s->user_pt[(va>>12)&0x1ffULL]=0;
    return 0;
}

int paging_user_translate(const user_space_t *s,uint64_t va,uintptr_t *phys,uint64_t *flags){
    if(!s||!s->valid||!user_window(va)) return -1;
    uint64_t e=s->user_pt[(va>>12)&0x1ffULL];
    if(!(e&ATM_PTE_P)||!(e&ATM_PTE_U)) return -1;
    if(phys) *phys=(uintptr_t)((e&ATM_PAGE_MASK)|(va&0xfffULL));
    if(flags) *flags=e;
    return 0;
}

int paging_user_range(const user_space_t *s,uint64_t va,size_t size,int write){
    if(!s||!size||!user_window(va)||size>ATM_USER_WINDOW_SIZE) return -1;
    uint64_t end=va+(uint64_t)size-1;
    if(end<va||!user_window(end)) return -1;
    uint64_t page=va&ATM_PAGE_MASK, last=end&ATM_PAGE_MASK;
    for(;;){
        uintptr_t p; uint64_t f;
        if(paging_user_translate(s,page,&p,&f)<0) return -1;
        if(write&&!(f&ATM_PTE_W)) return -1;
        if(page==last) break;
        page+=ATM_PAGE_SIZE;
    }
    return 0;
}

int paging_selftest(void){
    user_space_t s;
    uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    if(!page||paging_create_user_space(&s)<0) return -1;
    if(paging_map_user_page(&s,ATM_USER_BASE,(uintptr_t)page,ATM_PTE_W)<0) return -1;
    uintptr_t p=0; uint64_t f=0;
    if(paging_user_translate(&s,ATM_USER_BASE+37,&p,&f)<0) return -1;
    if(p!=(uintptr_t)page+37 || !(f&ATM_PTE_W)) return -1;
    if(paging_user_range(&s,ATM_USER_BASE,128,1)<0) return -1;
    if(paging_unmap_user_page(&s,ATM_USER_BASE)<0) return -1;
    return paging_user_range(&s,ATM_USER_BASE,1,0)<0?0:-1;
}
