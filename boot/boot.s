/* boot.s — QewoxOS1 x86-64
 * Multiboot2 header → GRUB loads us in 32-bit protected mode,
 * then we set up long mode ourselves before calling kernel_main.
 *
 * Memory map:
 *   0x1000  — PML4  (page table level 4)
 *   0x2000  — PDPT  (page directory pointer table, 4 entries used)
 *   0x3000  — PD #0 (page directory, 2MB pages,  0GB-1GB)
 *   0x4000  — PD #1 (page directory, 2MB pages,  1GB-2GB)
 *   0x5000  — PD #2 (page directory, 2MB pages,  2GB-3GB)
 *   0x6000  — PD #3 (page directory, 2MB pages,  3GB-4GB)
 *   0x7E00  — initial 16KB stack
 *   1MB+    — kernel image
 *
 * Identity-maps the first 4GB of physical memory. This matters on real
 * hardware: firmware-provided framebuffers (and other MMIO BARs) are
 * frequently placed above the 1GB mark once a system has a few GB of
 * RAM plus integrated graphics with a stolen-memory window — mapping
 * only 0-1GB leaves those addresses unmapped, causing a silent page
 * fault (or worse, a triple fault / reset) the moment the kernel tries
 * to draw to the screen. QEMU's default LFB placement happens to sit
 * below 1GB, which is why this bug was invisible there.
 */

.set MB2_MAGIC,      0xE85250D6
.set MB2_ARCH,       0          /* i386 protected mode entry */
.set MB2_LEN,        (mb2_end - mb2_start)
.set MB2_CHECKSUM,   -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

/* Allocatable section: the Multiboot2 header must live in the loadable file image. */
.section .multiboot2, "a"
.align 8
mb2_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LEN
    .long MB2_CHECKSUM
    /* framebuffer tag */
    .short 5; .short 1; .long 20; .long 800; .long 600; .long 32
    .align 8
    /* end tag */
    .short 0; .short 0; .long 8
mb2_end:

/* ── Page tables: their own section, NOT zeroed by the BSS-clear loop.
 * They are fully populated by hand before CR3 is loaded, so they don't
 * need pre-zeroing — and critically, they must survive the BSS zero
 * pass that runs afterward in 64-bit mode, since by then CR3 already
 * points at them. Putting them in .bss and zeroing .bss after loading
 * CR3 wiped out the live page tables on every boot; it only "worked"
 * under QEMU because the TLB kept serving cached translations for the
 * low 2MB the kernel actually touched. The instant code reaches a
 * page that wasn't already TLB-cached (e.g. a framebuffer placed above
 * 1GB on real hardware) the CPU walks the now-zeroed tables and faults
 * — this was the root cause of the black screen on real hardware. */
.section .pagetables, "aw", @nobits
.align 4096
pml4:  .skip 4096
pdpt:  .skip 4096
pd0:   .skip 4096   /* 0GB - 1GB */
pd1:   .skip 4096   /* 1GB - 2GB */
pd2:   .skip 4096   /* 2GB - 3GB */
pd3:   .skip 4096   /* 3GB - 4GB */

/* ── BSS: stack only — page tables live in .pagetables above ── */
.section .bss
.align 16
stack_bottom: .skip 65536   /* 64KB stack */
stack_top:

/* ── 32-bit entry (GRUB hands us control here) ──────────── */
.section .text
.code32
.global _start
.type _start, @function
_start:
    /* Save multiboot info before touching registers */
    movl %eax, %edi          /* magic   → edi (1st arg for kernel_main) */
    movl %ebx, %esi          /* mbinfo  → esi (2nd arg for kernel_main) */

    /* Disable interrupts */
    cli

    /* Verify we can use CPUID / long mode */
    /* (skip check — QEMU always supports x64) */

    /* ── Build identity-map page tables (0 → 4GB, 2MB pages) ── */
    /* PML4[0] = &PDPT | Present | Writable */
    movl $(pdpt + 0x03), %eax
    movl %eax, (pml4)
    movl $0, (pml4 + 4)

    /* PDPT[0..3] = &PD0..PD3 | Present | Writable — four 1GB regions */
    movl $(pd0 + 0x03), %eax
    movl %eax, (pdpt)
    movl $0, (pdpt + 4)

    movl $(pd1 + 0x03), %eax
    movl %eax, (pdpt + 8)
    movl $0, (pdpt + 12)

    movl $(pd2 + 0x03), %eax
    movl %eax, (pdpt + 16)
    movl $0, (pdpt + 20)

    movl $(pd3 + 0x03), %eax
    movl %eax, (pdpt + 24)
    movl $0, (pdpt + 28)

    /* PD0: map 512 × 2MB entries = bytes [0GB, 1GB) */
    movl $0, %ecx
.fill_pd0:
    movl %ecx, %eax
    shll $21, %eax           /* entry * 2MB */
    orl  $0x83, %eax         /* Present | Writable | HugePage */
    movl %eax, (pd0)(,%ecx,8)
    movl $0,   (pd0 + 4)(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne  .fill_pd0

    /* PD1: bytes [1GB, 2GB) — base offset 0x40000000 */
    movl $0, %ecx
.fill_pd1:
    movl %ecx, %eax
    shll $21, %eax
    addl $0x40000000, %eax
    orl  $0x83, %eax
    movl %eax, (pd1)(,%ecx,8)
    movl $0,   (pd1 + 4)(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne  .fill_pd1

    /* PD2: bytes [2GB, 3GB) — base offset 0x80000000 */
    movl $0, %ecx
.fill_pd2:
    movl %ecx, %eax
    shll $21, %eax
    addl $0x80000000, %eax
    orl  $0x83, %eax
    movl %eax, (pd2)(,%ecx,8)
    movl $0,   (pd2 + 4)(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne  .fill_pd2

    /* PD3: bytes [3GB, 4GB) — base offset 0xC0000000 */
    movl $0, %ecx
.fill_pd3:
    movl %ecx, %eax
    shll $21, %eax
    addl $0xC0000000, %eax
    orl  $0x83, %eax
    movl %eax, (pd3)(,%ecx,8)
    movl $0,   (pd3 + 4)(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne  .fill_pd3

    /* ── Enable PAE ── */
    movl %cr4, %eax
    orl  $0x20, %eax         /* PAE bit */
    movl %eax, %cr4

    /* ── Load PML4 into CR3 ── */
    movl $pml4, %eax
    movl %eax, %cr3

    /* ── Enable long mode in EFER MSR ── */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x900, %eax        /* LME + NXE: user stack PTEs use NX */
    wrmsr

    /* ── Enable paging → activates long mode (CR0.PG + CR0.PE) ── */
    movl %cr0, %eax
    orl  $0x80000001, %eax
    movl %eax, %cr0

    /* ── Load 64-bit GDT and far-jump to 64-bit code ── */
    lgdt (gdt64_ptr)
    ljmp $0x08, $.Llong_mode

/* ── 64-bit code segment ─────────────────────────────────── */
.code64
/* Native UEFI entry: firmware remains in long mode. RDI=MB2 magic,
 * RSI=minimal framebuffer info. Build the kernel's own identity map before
 * entering the common long-mode setup below. */
.global uefi_start
.type uefi_start,@function
uefi_start:
    cli
    /* UEFI is already in long mode, but the kernel's user mappings use the
     * NX bit. Ensure EFER.NXE is set before they become active. */
    movl $0xC0000080,%ecx
    rdmsr
    orl  $0x800,%eax
    wrmsr
    lgdt gdt64_ptr(%rip)
    pushq $0x08
    leaq .Luefi_kernel_cs(%rip),%rax
    pushq %rax
    lretq                          /* reload CS from the kernel GDT */
.Luefi_kernel_cs:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    movw %ax,%ss
    movabsq $pdpt,%rax
    orq $0x03,%rax
    movq %rax,pml4
    movabsq $pd0,%rax
    orq $0x03,%rax
    movq %rax,pdpt
    movabsq $pd1,%rax
    orq $0x03,%rax
    movq %rax,pdpt+8
    movabsq $pd2,%rax
    orq $0x03,%rax
    movq %rax,pdpt+16
    movabsq $pd3,%rax
    orq $0x03,%rax
    movq %rax,pdpt+24
    xorq %rcx,%rcx
.Luefi_pd0:
    movq %rcx,%rax
    shlq $21,%rax
    orq $0x83,%rax
    movq %rax,pd0(,%rcx,8)
    movq %rcx,%rax
    shlq $21,%rax
    addq $0x40000000,%rax
    orq $0x83,%rax
    movq %rax,pd1(,%rcx,8)
    movq %rcx,%rax
    shlq $21,%rax
    movabsq $0x80000000,%rdx
    addq %rdx,%rax
    orq $0x83,%rax
    movq %rax,pd2(,%rcx,8)
    movq %rcx,%rax
    shlq $21,%rax
    movabsq $0xC0000000,%rdx
    addq %rdx,%rax
    orq $0x83,%rax
    movq %rax,pd3(,%rcx,8)
    incq %rcx
    cmpq $512,%rcx
    jne .Luefi_pd0
    movabsq $pml4,%rax
    movq %rax,%cr3
    jmp .Llong_mode

.Llong_mode:
    /* Set up segment registers */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

        /* Set up stack */
    movabsq $stack_top, %rsp
    /* Preserve Multiboot arguments: RDI is needed as the BSS clear pointer. */
    movl %edi, %r12d
    movl %esi, %r13d
    /* Zero BSS */
    movabsq $bss_start, %rdi
    movabsq $bss_end,   %rcx
    subq %rdi, %rcx
    shrq $3, %rcx
    xorq %rax, %rax
    rep stosq
    /* Call kernel_main(magic, mbinfo_phys) with the original arguments. */
    movl %r12d, %edi
    movl %r13d, %esi
    movabsq $kernel_main, %rax
    call *%rax

    cli
.Lhalt:
    hlt
    jmp .Lhalt

/* ── Minimal 64-bit GDT (used for long-mode jump only) ───── */
.section .data
.align 8
.global gdt64
gdt64:
    .quad 0x0000000000000000      /* 0: null */
    .quad 0x00AF9A000000FFFF      /* 1: 64-bit kernel code  CS=0x08 */
    .quad 0x00AF92000000FFFF      /* 2: 64-bit kernel data  DS=0x10 */
    .quad 0x00AFFA000000FFFF      /* 3: 64-bit user code    CS=0x18 */
    .quad 0x00AFF2000000FFFF      /* 4: 64-bit user data    DS=0x20 */
    .quad 0x0000000000000000      /* 5-6: runtime TSS descriptor */
    .quad 0x0000000000000000
gdt64_end:

.global gdt64_ptr
gdt64_ptr:
    .short (gdt64_end - gdt64 - 1)
    .quad  gdt64

/* ── ISR / IRQ stubs ─────────────────────────────────────── */
.section .text
.code64

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    pushq $0
    pushq $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    pushq $\num
    jmp isr_common_stub
.endm

.macro IRQ num idt_num
.global irq\num
irq\num:
    cli
    pushq $0
    pushq $\idt_num
    jmp irq_common_stub
.endm

ISR_NOERRCODE 0;  ISR_NOERRCODE 1;  ISR_NOERRCODE 2;  ISR_NOERRCODE 3
ISR_NOERRCODE 4;  ISR_NOERRCODE 5;  ISR_NOERRCODE 6;  ISR_NOERRCODE 7
ISR_ERRCODE   8;  ISR_NOERRCODE 9;  ISR_ERRCODE   10; ISR_ERRCODE   11
ISR_ERRCODE   12; ISR_ERRCODE   13; ISR_ERRCODE   14; ISR_NOERRCODE 15
ISR_NOERRCODE 16; ISR_NOERRCODE 17; ISR_NOERRCODE 18; ISR_NOERRCODE 19
ISR_NOERRCODE 20; ISR_NOERRCODE 21; ISR_NOERRCODE 22; ISR_NOERRCODE 23
ISR_NOERRCODE 24; ISR_NOERRCODE 25; ISR_NOERRCODE 26; ISR_NOERRCODE 27
ISR_NOERRCODE 28; ISR_NOERRCODE 29; ISR_NOERRCODE 30; ISR_NOERRCODE 31

.global isr128
isr128:
    cli
    pushq $0
    pushq $128
    jmp isr_common_stub

IRQ 0,32;  IRQ 1,33;  IRQ 2,34;  IRQ 3,35;  IRQ 4,36;  IRQ 5,37
IRQ 6,38;  IRQ 7,39;  IRQ 8,40;  IRQ 9,41;  IRQ 10,42; IRQ 11,43
IRQ 12,44; IRQ 13,45; IRQ 14,46; IRQ 15,47

/* ── Common ISR stub (64-bit) ────────────────────────────── */
.extern isr_handler
isr_common_stub:
    /* Save all caller-saved registers */
    pushq %rax; pushq %rcx; pushq %rdx; pushq %rsi
    pushq %rdi; pushq %r8;  pushq %r9;  pushq %r10; pushq %r11
    /* Remaining callee-saved (for completeness) */
    pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15

    movq %rsp, %rdi          /* pass stack frame pointer as argument */
    call isr_handler

    popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx
    popq %r11; popq %r10; popq %r9;  popq %r8
    popq %rdi; popq %rsi; popq %rdx; popq %rcx; popq %rax
    addq $16, %rsp           /* pop int_no + err_code */
    iretq

/* ── Linux-shaped x86-64 syscall entry ───────────────────────
 * The CPU supplies RCX=user RIP and R11=user RFLAGS. Build the same
 * registers_t layout as the int $0x80 path on the current task's known
 * kernel stack, dispatch only through linux_syscall_dispatch(), then IRETQ
 * to preserve the existing scheduler/user-return model. RCX/R11 remain
 * syscall-clobbered as required by the Linux x86-64 ABI. */
.global linux_syscall_entry
.extern linux_syscall_dispatch
.extern linux_kernel_stack_top
.extern linux_saved_user_rsp
linux_syscall_entry:
    cli
    movq %rsp,linux_saved_user_rsp(%rip)
    movq linux_kernel_stack_top(%rip),%rsp
    pushq $0x23
    pushq linux_saved_user_rsp(%rip)
    pushq %r11
    pushq $0x1b
    pushq %rcx
    pushq $0
    pushq $128
    pushq %rax; pushq %rcx; pushq %rdx; pushq %rsi
    pushq %rdi; pushq %r8;  pushq %r9;  pushq %r10; pushq %r11
    pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
    movq %rsp,%rdi
    call linux_syscall_dispatch
    movq %rax,112(%rsp)
    popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx
    popq %r11; popq %r10; popq %r9;  popq %r8
    popq %rdi; popq %rsi; popq %rdx; popq %rcx; popq %rax
    addq $16,%rsp
    iretq

/* ── Common IRQ stub (64-bit) ────────────────────────────── */
.extern irq_handler
irq_common_stub:
    pushq %rax; pushq %rcx; pushq %rdx; pushq %rsi
    pushq %rdi; pushq %r8;  pushq %r9;  pushq %r10; pushq %r11
    pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15

    movq %rsp, %rdi
    call irq_handler

    popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx
    popq %r11; popq %r10; popq %r9;  popq %r8
    popq %rdi; popq %rsi; popq %rdx; popq %rcx; popq %rax
    addq $16, %rsp
    iretq

/* gdt_flush / idt_flush not needed in x64 — handled inline in gdt.c/idt.c */

/* ── context_switch(cpu_context_t *old, cpu_context_t *new) ──
 * rdi = old context pointer, rsi = new context pointer
 * Saves callee-saved regs + rsp + rip into old, restores from new.
 * Offsets match cpu_context_t: r15(0) r14(8) r13(16) r12(24)
 *                              rbp(32) rbx(40) rsp(48) rip(56) rflags(64)
 */
.global context_switch
/* Controlled transition to native CPL 3. Arguments:
 * RDI=target CR3, RSI=kernel stack top (TSS.rsp0 already set by C),
 * RDX=user RSP, RCX=user RIP. User IF stays clear until user scheduling
 * has a complete save/restore path. */
.global ring3_enter
ring3_enter:
    cli
    movq %rdi, %cr3
    movq %rsi, %rsp
    pushq $0x23
    pushq %rdx
    pushq $0x0000000000000002
    pushq $0x1b
    pushq %rcx
    iretq

context_switch:
    /* Save callee-saved registers into old context (rdi) */
    movq  %r15,  0(%rdi)
    movq  %r14,  8(%rdi)
    movq  %r13, 16(%rdi)
    movq  %r12, 24(%rdi)
    movq  %rbp, 32(%rdi)
    movq  %rbx, 40(%rdi)
    /* Save a resumable post-call stack. context_switch restores RSP and
     * JMPs directly to the return address instead of executing RET; retaining
     * the call's return address on the restored stack makes the interrupted
     * scheduler frame return twice and corrupts a later IRETQ. */
    leaq  8(%rsp), %rax
    movq  %rax, 48(%rdi)
    /* Save the instruction immediately after this call. */
    movq  (%rsp), %rax
    movq  %rax,  56(%rdi)
    /* Save rflags and current address-space root. */
    pushfq
    popq  %rax
    movq  %rax,  64(%rdi)
    movq  %cr3, %rax
    movq  %rax,  72(%rdi)

    /* Restore from new context (rsi) */
    movq   0(%rsi), %r15
    movq   8(%rsi), %r14
    movq  16(%rsi), %r13
    movq  24(%rsi), %r12
    movq  32(%rsi), %rbp
    movq  40(%rsi), %rbx
    /* Restore rflags */
    movq  64(%rsi), %rax
    pushq %rax
    popfq
    /* Switch address space before restoring the new task stack. */
    movq  72(%rsi), %rax
    testq %rax, %rax
    jz    .Lctx_no_cr3
    movq  %rax, %cr3
.Lctx_no_cr3:
    /* Switch stack */
    movq  48(%rsi), %rsp
    /* Jump to saved rip */
    movq  56(%rsi), %rax
    jmp  *%rax


.section .note.GNU-stack,"",@progbits
