#include "gdt.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

extern uint64_t gdt64[];

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr64_t;

static tss64_t kernel_tss;

void gdt_install(void){
    kmemset(&kernel_tss,0,sizeof(kernel_tss));
    kernel_tss.iomap_base=(uint16_t)sizeof(kernel_tss);
    uint64_t base=(uint64_t)(uintptr_t)&kernel_tss;
    uint64_t limit=(uint64_t)sizeof(kernel_tss)-1;
    /* TSS descriptor occupies GDT slots 5 and 6 (selector 0x28). */
    gdt64[5]=(limit&0xffffULL)|((base&0xffffffULL)<<16)|
             (0x89ULL<<40)|((limit&0xf0000ULL)<<48)|((base&0xff000000ULL)<<32);
    gdt64[6]=base>>32;
    gdtr64_t gdtr={(uint16_t)(7*8-1),(uint64_t)(uintptr_t)gdt64};
    __asm__ volatile("lgdt %0"::"m"(gdtr));
    uint16_t sel=0x28;
    __asm__ volatile("ltr %0"::"r"(sel));
}

void gdt_set_kernel_stack(uint64_t rsp0){ kernel_tss.rsp0=rsp0; }
