/* idt.c — x86-64 IDT + PIC setup for atmkoala */
#include "idt.h"
#include "kernel_panic.h"
#include "atm_syscall.h"
#include "vga.h"
#include "util.h"
#include <stdint.h>

#define IDT_ENTRIES 256

static idt_entry64_t idt_entries[IDT_ENTRIES];
static idt_ptr64_t   idt_ptr;
static irq_handler_t irq_routines[16] = { 0 };

/* ── PIC ─────────────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(void) {
    uint8_t m1 = inb(PIC1_DATA);
    uint8_t m2 = inb(PIC2_DATA);
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

/* ── Gate setup (64-bit interrupt gate) ──────────────────── */
static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_lo  = (uint16_t)(base & 0xFFFF);
    idt_entries[num].base_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt_entries[num].base_hi  = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt_entries[num].selector = sel;
    idt_entries[num].ist      = 0;
    idt_entries[num].flags    = flags;
    idt_entries[num].reserved = 0;
}

void idt_install(void) {
    idt_ptr.limit = sizeof(idt_entry64_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint64_t)&idt_entries;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_entries[i].base_lo = idt_entries[i].base_mid = 0;
        idt_entries[i].base_hi = idt_entries[i].reserved = 0;
        idt_entries[i].selector = 0;
        idt_entries[i].ist = idt_entries[i].flags = 0;
    }

    pic_remap();

#define SG(n, fn) idt_set_gate(n, (uint64_t)(fn), 0x08, 0x8E)
    SG(0,isr0);  SG(1,isr1);  SG(2,isr2);  SG(3,isr3);
    SG(4,isr4);  SG(5,isr5);  SG(6,isr6);  SG(7,isr7);
    SG(8,isr8);  SG(9,isr9);  SG(10,isr10); SG(11,isr11);
    SG(12,isr12); SG(13,isr13); SG(14,isr14); SG(15,isr15);
    SG(16,isr16); SG(17,isr17); SG(18,isr18); SG(19,isr19);
    SG(20,isr20); SG(21,isr21); SG(22,isr22); SG(23,isr23);
    SG(24,isr24); SG(25,isr25); SG(26,isr26); SG(27,isr27);
    SG(28,isr28); SG(29,isr29); SG(30,isr30); SG(31,isr31);
    SG(128, isr128);
    SG(32,irq0);  SG(33,irq1);  SG(34,irq2);  SG(35,irq3);
    SG(36,irq4);  SG(37,irq5);  SG(38,irq6);  SG(39,irq7);
    SG(40,irq8);  SG(41,irq9);  SG(42,irq10); SG(43,irq11);
    SG(44,irq12); SG(45,irq13); SG(46,irq14); SG(47,irq15);
#undef SG

    __asm__ volatile("lidt %0" :: "m"(idt_ptr));
}

void idt_enable_user_syscall_gate(void) {
    /* 0xEE = present, DPL 3, 64-bit interrupt gate.  This remains closed
     * until usermode_init() has installed a valid TSS.rsp0 path. */
    idt_set_gate(128,(uint64_t)isr128,0x08,0xEE);
}

void irq_install_handler(int irq, irq_handler_t h) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = h;
}
void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = 0;
}

/* ── ISR handler ─────────────────────────────────────────── */
void isr_handler(registers_t *r) {
    if (r->int_no == 128) { r->rax=atm_syscall_dispatch(r); return; }
    kernel_panic_exception(r);
}

/* ── IRQ handler ─────────────────────────────────────────── */
void irq_handler(registers_t *r) {
    uint8_t irq = (uint8_t)(r->int_no - 32);
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
    if (irq < 16 && irq_routines[irq])
        irq_routines[irq](r);
}
