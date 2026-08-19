/* ATMBOOT stage 0 — BIOS MBR loader.
 * Loads the fixed-size stage 2 from LBA 1 to 0000:8000 through EDD INT 13h.
 * Stage 0 stays partition-table compatible: bytes 0x1BE..0x1FD are zero and
 * may later be populated by the installer without changing this loader. */
.code16
.section .text
.global _start
_start:
    cli
    xorw %ax,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $0x7C00,%sp
    sti
    movb %dl,boot_drive

    movw $dap,%si
    movb $0x42,%ah
    int $0x13
    jc disk_error
    ljmp $0x0800,$0x0000

disk_error:
    movw $error_text,%si
.print:
    lodsb
    testb %al,%al
    jz .halt
    movb $0x0E,%ah
    movb $0x07,%bl
    int $0x10
    jmp .print
.halt:
    cli
1:  hlt
    jmp 1b

boot_drive: .byte 0
.align 4
dap:
    .byte 0x10,0x00
    .word 64                 /* stage 2 occupies fixed LBAs 1..64 */
    .word 0x0000,0x0800      /* physical 0x8000 */
    .long 1,0                /* LBA 1 */
error_text: .asciz "ATMBOOT: stage2 read error"

/* Preserve MBR partition table region for future disk install support. */
.org 0x1BE
.space 64,0
.word 0xAA55
