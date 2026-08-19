OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Native ET_EXEC images use separate RX and RW segments. This preserves the
 * existing 0x40000000 user base while preventing a writable code segment. */
PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R | PF_X */
    data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x40000000;
    .text : ALIGN(16) { *(.text.start) *(.text .text.*) } :text
    .rodata : ALIGN(16) { *(.rodata .rodata.*) } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : ALIGN(16) { *(.data .data.*) } :data
    .bss : ALIGN(16) { *(.bss .bss.* COMMON) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
