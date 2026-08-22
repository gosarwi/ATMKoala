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
static uint64_t user_pt_slot(uint64_t va){ return (va-ATM_USER_BASE)>>21; }
static uint64_t user_pt_page(uint64_t va){ return (va>>12)&0x1ffULL; }
static void user_tlb_invalidate(uint64_t va){
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)va):"memory");
}

void paging_init(void){
    g_kernel_cr3=(uintptr_t)(cpu_cr3() & ATM_PAGE_MASK);
}
uintptr_t paging_kernel_cr3(void){ return g_kernel_cr3; }
int paging_activate_user_space(const user_space_t *space){
    if(!space || !space->valid || !space->cr3) return -1;
    __asm__ volatile("mov %0,%%cr3"::"r"((uint64_t)space->cr3):"memory");
    return 0;
}

int paging_create_user_space(user_space_t *s){
    if(!s) return -1;
    kmemset(s,0,sizeof(*s));
    if(!g_kernel_cr3) paging_init();
    uint64_t *kpml4=(uint64_t *)(uintptr_t)g_kernel_cr3;
    uint64_t *kpdpt=phys_ptr(kpml4[0]);
    if(!(kpml4[0]&ATM_PTE_P) || !kpdpt) return -1;

    s->pml4=page_alloc(); s->pdpt=page_alloc();
    s->user_pt=(uint64_t **)kmalloc(sizeof(*s->user_pt)*ATM_USER_PT_COUNT);
    if(s->user_pt) kmemset(s->user_pt,0,sizeof(*s->user_pt)*ATM_USER_PT_COUNT);
    if(!s->pml4||!s->pdpt||!s->user_pt){ paging_destroy_user_space(s); return -1; }
    for(int i=0;i<4;i++){
        s->pd[i]=page_alloc();
        if(!s->pd[i]){ paging_destroy_user_space(s); return -1; }
    }

    kmemcpy(s->pml4,kpml4,ATM_PAGE_SIZE);
    kmemcpy(s->pdpt,kpdpt,ATM_PAGE_SIZE);
    for(int i=0;i<4;i++){
        uint64_t e=kpdpt[i];
        if(!(e&ATM_PTE_P)){ paging_destroy_user_space(s); return -1; }
        uint64_t *kpd=phys_ptr(e);
        kmemcpy(s->pd[i],kpd,ATM_PAGE_SIZE);
        s->pdpt[i]=((uint64_t)(uintptr_t)s->pd[i]&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W;
    }

    /* 0x40000000 lies in PML4[0], PDPT[1], PD[0]. Each 2 MiB part of
     * the bounded region obtains a distinct 4 KiB page table on first map. */
    s->pml4[0]=((uint64_t)(uintptr_t)s->pdpt&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    s->pdpt[1]=((uint64_t)(uintptr_t)s->pd[1]&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    s->cr3=(uintptr_t)s->pml4;
    s->valid=1;
    return 0;
}

int paging_map_user_page(user_space_t *s,uint64_t va,uintptr_t phys,uint64_t flags){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    uint64_t slot=user_pt_slot(va), pi=user_pt_page(va);
    if(slot>=ATM_USER_PT_COUNT) return -1;
    if(!s->user_pt[slot]){
        s->user_pt[slot]=page_alloc();
        if(!s->user_pt[slot]) return -1;
        s->pd[1][slot]=((uint64_t)(uintptr_t)s->user_pt[slot]&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_W|ATM_PTE_U;
    }
    s->user_pt[slot][pi]=((uint64_t)phys&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_U|(flags&ATM_PTE_W)|(flags&ATM_PTE_NX);
    uint64_t page=(va-ATM_USER_BASE)/ATM_PAGE_SIZE;
    if(page>=s->mapped_pages) s->mapped_pages=page+1;
    user_tlb_invalidate(va);
    return 0;
}
int paging_unmap_user_page(user_space_t *s,uint64_t va){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    uint64_t slot=user_pt_slot(va);
    if(slot>=ATM_USER_PT_COUNT||!s->user_pt[slot]) return -1;
    s->user_pt[slot][user_pt_page(va)]=0;
    user_tlb_invalidate(va);
    return 0;
}

int paging_release_user_page(user_space_t *s,uint64_t va){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    uint64_t slot=user_pt_slot(va);
    if(slot>=ATM_USER_PT_COUNT||!s->user_pt[slot]) return -1;
    uint64_t old=s->user_pt[slot][user_pt_page(va)];
    if(!(old&ATM_PTE_P)||!(old&ATM_PTE_U)) return -1;
    s->user_pt[slot][user_pt_page(va)]=0;
    user_tlb_invalidate(va);
    kfree((void *)(uintptr_t)(old&ATM_PAGE_MASK));
    return 0;
}

int paging_protect_user_page(user_space_t *s,uint64_t va,uint64_t flags){
    if(!s||!s->valid||!user_window(va)||(va&0xfff)) return -1;
    uint64_t slot=user_pt_slot(va);
    if(slot>=ATM_USER_PT_COUNT||!s->user_pt[slot]) return -1;
    uint64_t old=s->user_pt[slot][user_pt_page(va)];
    if(!(old&ATM_PTE_P)||!(old&ATM_PTE_U)) return -1;
    s->user_pt[slot][user_pt_page(va)]=(old&ATM_PAGE_MASK)|ATM_PTE_P|ATM_PTE_U|
                                      (flags&ATM_PTE_W)|(flags&ATM_PTE_NX);
    user_tlb_invalidate(va);
    return 0;
}

uint64_t paging_user_mapped_bytes(const user_space_t *s){
    if(!s || !s->valid) return 0;
    uint64_t pages=0;
    for(uint64_t slot=0;slot<ATM_USER_PT_COUNT;slot++) if(s->user_pt[slot])
        for(uint64_t i=0;i<512;i++) if(s->user_pt[slot][i]&ATM_PTE_P) pages++;
    return pages*ATM_PAGE_SIZE;
}

void paging_destroy_user_space(user_space_t *s){
    if(!s) return;
    /* Native ELF pages are retained until waitpid() so an exiting CPL 3 task
     * never frees its active CR3. Reaping calls this after it is off-CPU. */
    if(s->user_pt){
        for(uint64_t slot=0;slot<ATM_USER_PT_COUNT;slot++) if(s->user_pt[slot]){
            for(uint64_t i=0;i<512;i++){
                uint64_t entry=s->user_pt[slot][i];
                if(entry&ATM_PTE_P) kfree((void *)(uintptr_t)(entry&ATM_PAGE_MASK));
            }
            kfree(s->user_pt[slot]);
        }
        kfree(s->user_pt);
    }
    for(int i=0;i<4;i++) if(s->pd[i]) kfree(s->pd[i]);
    if(s->pdpt) kfree(s->pdpt);
    if(s->pml4) kfree(s->pml4);
    kmemset(s,0,sizeof(*s));
}

int paging_user_translate(const user_space_t *s,uint64_t va,uintptr_t *phys,uint64_t *flags){
    if(!s||!s->valid||!user_window(va)) return -1;
    uint64_t slot=user_pt_slot(va);
    if(slot>=ATM_USER_PT_COUNT||!s->user_pt[slot]) return -1;
    uint64_t e=s->user_pt[slot][user_pt_page(va)];
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
    uint8_t *low=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    uint8_t *high=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    int low_mapped=0,high_mapped=0,rc=-1;
    uint64_t high_va=ATM_USER_BASE+0x200000ULL;
    if(!low||!high){if(low)kfree(low);if(high)kfree(high);return -1;}
    if(paging_create_user_space(&s)<0){kfree(low);kfree(high);return -1;}
    if(paging_map_user_page(&s,ATM_USER_BASE,(uintptr_t)low,ATM_PTE_W)<0) goto done;
    low_mapped=1;
    if(paging_map_user_page(&s,high_va,(uintptr_t)high,ATM_PTE_W)<0) goto done;
    high_mapped=1;
    uintptr_t p=0; uint64_t f=0;
    if(paging_user_translate(&s,ATM_USER_BASE+37,&p,&f)<0) goto done;
    if(p!=(uintptr_t)low+37 || !(f&ATM_PTE_W)) goto done;
    if(paging_user_translate(&s,high_va+91,&p,&f)<0) goto done;
    if(p!=(uintptr_t)high+91 || !(f&ATM_PTE_W)) goto done;
    if(paging_user_range(&s,ATM_USER_BASE,128,1)<0) goto done;
    if(paging_user_range(&s,high_va,128,1)<0) goto done;
    if(paging_user_mapped_bytes(&s)!=2*ATM_PAGE_SIZE) goto done;
    if(paging_unmap_user_page(&s,ATM_USER_BASE)<0) goto done;
    low_mapped=0;
    rc=paging_user_range(&s,ATM_USER_BASE,1,0)<0?0:-1;

done:
    paging_destroy_user_space(&s);
    if(!low_mapped) kfree(low);
    if(!high_mapped) kfree(high);
    return rc;
}

