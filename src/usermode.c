#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "kmalloc.h"
#include "util.h"
#include <stdint.h>

extern void ring3_enter(uint64_t cr3,uint64_t kernel_stack_top,
                        uint64_t user_rsp,uint64_t user_rip);

static int gate_ready=0;

void usermode_init(void){
    /* IDT installation retains the syscall vector; this flag records that
     * the runtime TSS is live and a valid CPL 3 entry is now possible. */
    idt_enable_user_syscall_gate();
    gate_ready=1;
}
int usermode_gate_ready(void){ return gate_ready; }

__attribute__((noreturn)) void usermode_enter(atm_user_context_t *ctx){
    if(!ctx||!ctx->space||!ctx->space->valid||!gate_ready||
       ctx->entry<ATM_USER_BASE||ctx->entry>=ATM_USER_TOP||
       ctx->stack_top<=ATM_USER_BASE||ctx->stack_top>ATM_USER_TOP)
        for(;;) __asm__ volatile("cli; hlt");
    gdt_set_kernel_stack(ctx->kernel_stack_top);
    ring3_enter((uint64_t)ctx->space->cr3,ctx->kernel_stack_top,
                ctx->stack_top,ctx->entry);
    __builtin_unreachable();
}

__attribute__((noreturn)) void usermode_selftest_enter(void){
    static const uint8_t code[]={
        0x48,0xC7,0xC0,0x00,0xA7,0x00,0x00, /* mov rax,ATM_SYS_ABI_INFO */
        0xCD,0x80,                           /* int 0x80 */
        0xEB,0xFE                            /* jmp $ */
    };
    user_space_t *space=(user_space_t *)kmalloc(sizeof(user_space_t));
    uint8_t *code_page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    uint8_t *stack_page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    uint8_t *kstack=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,16);
    if(!space||!code_page||!stack_page||!kstack||paging_create_user_space(space)<0)
        for(;;) __asm__ volatile("cli; hlt");
    kmemcpy(code_page,code,sizeof(code));
    if(paging_map_user_page(space,ATM_USER_BASE,(uintptr_t)code_page,0)<0 ||
       paging_map_user_page(space,ATM_USER_STACK_TOP,(uintptr_t)stack_page,ATM_PTE_W)<0)
        for(;;) __asm__ volatile("cli; hlt");
    atm_user_context_t ctx={space,ATM_USER_BASE,ATM_USER_TOP,(uint64_t)(uintptr_t)(kstack+ATM_PAGE_SIZE)};
    usermode_enter(&ctx);
}
