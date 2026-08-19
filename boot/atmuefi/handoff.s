.section .text
.code64
.global atmuefi_handoff
.type atmuefi_handoff,@function
/* RCX = low physical Multiboot2-compatible information pointer (MS x64 ABI).
 * UEFI already executes in 64-bit long mode; uefi_start takes ownership by
 * installing kernel page tables and the kernel GDT before common init. */
atmuefi_handoff:
    cli
    movq %rcx,%rsi
    movl $0x36D76289,%edi
    movabsq $0x04001158,%rax     /* checked uefi_start physical entry */
    jmp *%rax

.section .note.GNU-stack,"",@progbits
