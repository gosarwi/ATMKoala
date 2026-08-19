/* linker.ld — QewoxOS1 x86-64 */
ENTRY(_start)

SECTIONS {
    . = 64M;  /* UEFI AllocateAddress-safe region above firmware low allocations */

    /* GRUB scans the beginning of the file, so retain the header first. */
    .multiboot2 ALIGN(8)    : { KEEP(*(.multiboot2)) }
    .text       ALIGN(0x1000) : { *(.text) *(.text.*) }
    .rodata     ALIGN(0x1000) : { *(.rodata) *(.rodata.*) }
    .data       ALIGN(0x1000) : { *(.data) *(.data.*) }

    .pagetables ALIGN(0x1000) (NOLOAD) : { *(.pagetables) }

    bss_start = .;
    .bss ALIGN(0x1000) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    bss_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.GNU-stack)
        *(.note.gnu.*)
    }
}
