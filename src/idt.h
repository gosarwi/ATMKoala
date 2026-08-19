#ifndef IDT_H
#define IDT_H
#include <stdint.h>

/* x86-64 IDT gate: 16 bytes */
typedef struct __attribute__((packed)) {
    uint16_t base_lo;
    uint16_t selector;    /* kernel code segment: 0x08 */
    uint8_t  ist;         /* interrupt stack table: 0 */
    uint8_t  flags;       /* 0x8E = present, ring0, interrupt gate */
    uint16_t base_mid;
    uint32_t base_hi;
    uint32_t reserved;
} idt_entry64_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr64_t;

/* x86-64 interrupt frame pushed by CPU + our stubs */
typedef struct {
    /* saved by stub */
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rdx, rcx, rax;
    /* pushed by stub */
    uint64_t int_no, err_code;
    /* pushed by CPU */
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

typedef void (*irq_handler_t)(registers_t *);

void idt_install(void);
void idt_enable_user_syscall_gate(void);
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

/* ISR stubs declared in boot.s */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void isr128(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

/* Port I/O: see util.h */

#endif /* IDT_H */
