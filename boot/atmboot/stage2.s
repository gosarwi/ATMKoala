/* ATMBOOT stage 2 — BIOS branch of the unified ATM Loader.
 * It is linked at physical 0x8000 and hands a small Multiboot2-compatible
 * information block to the shared 64-bit kernel entry. */
.code16
.section .text
.global atmboot_stage2

.set MB2_BOOTLOADER_MAGIC, 0x36D76289
.set KERNEL_LBA,           65
.set KERNEL_SECTORS,       1024
.set KERNEL_ENTRY,         0x04001000
.set MBINFO,               0x5000
.set VBE_INFO,             0x6000

atmboot_stage2:
    cli
    xorw %ax,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $0x7C00,%sp
    sti
    call bios_menu
    cmpb $2,%al
    je mode_compat
    cmpb $3,%al
    je mode_installer
    /* Tile 1 / Enter: graphical desktop. */
mode_desktop:
    call set_vbe_or_text
    jmp enter_protected
mode_compat:
    call build_text_novbe_mbinfo
    jmp enter_protected
mode_installer:
    call set_vbe_or_text
    /* Installer requires graphics; fallback to compatible console if VBE fails. */
    cmpb $1,boot_has_vbe
    jne mode_compat
    call append_installer_cmdline
    jmp enter_protected

/* ATM Loader visual grid. BIOS text cells keep the menu available on every
 * VGA-compatible machine before a VBE framebuffer exists. */
bios_menu:
    movw $0x0003,%ax
    int $0x10
    movw $menu_text,%si
    call bios_puts
.wait:
    xorw %ax,%ax
    int $0x16
    cmpb $0x0D,%al
    je .desktop
    cmpb $'1',%al
    je .desktop
    cmpb $'2',%al
    je .compat
    cmpb $'3',%al
    je .installer
    cmpb $'4',%al
    je .restart
    jmp .wait
.desktop: movb $1,%al; ret
.compat:  movb $2,%al; ret
.installer: movb $3,%al; ret
.restart: int $0x19; jmp .restart

/* Normal/installer path requests VBE. The fallback is a standard text-only
 * Multiboot2 info block, preserving the same behavior as GRUB's novbe mode. */
set_vbe_or_text:
    movb $0,boot_has_vbe
    xorw %ax,%ax
    movw %ax,%es
    movw $VBE_INFO,%di
    movw $0x4F01,%ax
    movw $0x0118,%cx
    int $0x10
    cmpw $0x004F,%ax
    jne .fallback_text
    movw $0x4F02,%ax
    movw $0x4118,%bx
    int $0x10
    cmpw $0x004F,%ax
    jne .fallback_text
    call build_vbe_mbinfo
    movb $1,boot_has_vbe
    ret
.fallback_text:
    call build_text_mbinfo
    ret

/* Header(8) + end tag(8). */
build_text_mbinfo:
    movl $16,MBINFO
    movl $0,MBINFO+4
    movl $0,MBINFO+8
    movl $8,MBINFO+12
    ret

/* Header(8) + framebuffer tag(40) + end tag(8). */
build_vbe_mbinfo:
    movl $56,MBINFO
    movl $0,MBINFO+4
    movl $8,MBINFO+8
    movl $38,MBINFO+12
    movl VBE_INFO+0x28,%eax
    movl %eax,MBINFO+16
    movl $0,MBINFO+20
    movzwl VBE_INFO+0x10,%eax
    movl %eax,MBINFO+24
    movzwl VBE_INFO+0x12,%eax
    movl %eax,MBINFO+28
    movzwl VBE_INFO+0x14,%eax
    movl %eax,MBINFO+32
    movb VBE_INFO+0x19,%al
    movb %al,MBINFO+36
    movb $1,MBINFO+37
    movw $0,MBINFO+38
    movb VBE_INFO+0x20,%al
    movb %al,MBINFO+40
    movb VBE_INFO+0x1F,%al
    movb %al,MBINFO+41
    movb VBE_INFO+0x22,%al
    movb %al,MBINFO+42
    movb VBE_INFO+0x21,%al
    movb %al,MBINFO+43
    movb VBE_INFO+0x24,%al
    movb %al,MBINFO+44
    movb VBE_INFO+0x23,%al
    movb %al,MBINFO+45
    movl $0,MBINFO+48
    movl $8,MBINFO+52
    ret

/* Replace the existing end tag with cmdline `installer`, then append end.
 * cmd tag size is 18 and rounds up to 24 bytes. */
append_installer_cmdline:
    movl $80,MBINFO
    movl $1,MBINFO+48
    movl $18,MBINFO+52
    movl $0x74736e69,MBINFO+56 /* inst */
    movl $0x656c6c61,MBINFO+60 /* alle */
    movw $0x0072,MBINFO+64     /* r\0 */
    movl $0,MBINFO+72
    movl $8,MBINFO+76
    ret

/* Header + cmdline `novbe` + end. cmd tag size 14 rounds to 16. */
build_text_novbe_mbinfo:
    movl $32,MBINFO
    movl $0,MBINFO+4
    movl $1,MBINFO+8
    movl $14,MBINFO+12
    movl $0x62766f6e,MBINFO+16 /* novb */
    movw $0x0065,MBINFO+20     /* e\0 */
    movl $0,MBINFO+24
    movl $8,MBINFO+28
    ret

enter_protected:
    cli
    inb $0x92,%al
    orb $0x02,%al
    outb %al,$0x92
    lgdt gdt_desc
    movl %cr0,%eax
    orl $0x00000001,%eax
    movl %eax,%cr0
    ljmpl $0x08,$pm32_entry

.code32
pm32_entry:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movl $0x00090000,%esp
    call ata_load_kernel
    testl %eax,%eax
    jnz pm_disk_error
    movl $MB2_BOOTLOADER_MAGIC,%eax
    movl $MBINFO,%ebx
    movl $KERNEL_ENTRY,%ecx
    jmp *%ecx

/* Primary-master ATA PIO, fixed padded flat kernel payload. */
ata_load_kernel:
    movl $KERNEL_LBA,kernel_lba
    movl $KERNEL_SECTORS,kernel_left
    movl $0x04000000,%edi
.next_sector:
    cmpl $0,kernel_left
    je .success
    movl kernel_lba,%eax
    movl %eax,%ecx
    shrl $24,%ecx
    orb $0xE0,%cl
    movb %cl,%al
    movw $0x1F6,%dx
    outb %al,%dx
    movb $1,%al
    movw $0x1F2,%dx
    outb %al,%dx
    movl kernel_lba,%eax
    movw $0x1F3,%dx
    outb %al,%dx
    shrl $8,%eax
    movw $0x1F4,%dx
    outb %al,%dx
    shrl $8,%eax
    movw $0x1F5,%dx
    outb %al,%dx
    movb $0x20,%al
    movw $0x1F7,%dx
    outb %al,%dx
.wait_drq:
    movw $0x1F7,%dx
    inb %dx,%al
    testb $0x01,%al
    jnz .error
    testb $0x80,%al
    jnz .wait_drq
    testb $0x08,%al
    jz .wait_drq
    movw $0x1F0,%dx
    movl $256,%ecx
    rep insw
    incl kernel_lba
    decl kernel_left
    jmp .next_sector
.success: xorl %eax,%eax; ret
.error: movl $1,%eax; ret

pm_disk_error:
    movl $0xB8000,%edi
    movw $0x4F41,(%edi)
    movw $0x4F54,2(%edi)
    movw $0x4F4D,4(%edi)
    movw $0x4F42,6(%edi)
    movw $0x4F4F,8(%edi)
    movw $0x4F4F,10(%edi)
    movw $0x4F54,12(%edi)
    cli
1:  hlt
    jmp 1b

.code16
bios_puts:
    lodsb
    testb %al,%al
    jz .done
    movb $0x0E,%ah
    movb $0x07,%bl
    int $0x10
    jmp bios_puts
.done: ret

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:
gdt_desc:
    .word gdt_end-gdt-1
    .long gdt

kernel_lba:  .long 0
kernel_left: .long 0
boot_has_vbe: .byte 0
menu_text:
    .asciz "\r\n ATM LOADER  |  BIOS / ATMBOOT\r\n\r\n +----------------------+----------------------+\r\n | [1] EXP DESKTOP      | [2] COMPAT CONSOLE   |\r\n |     VBE / Exp        |     text-safe        |\r\n +----------------------+----------------------+\r\n | [3] DISK INSTALLER   | [4] RESTART          |\r\n |     graphical setup  |     firmware reset   |\r\n +----------------------+----------------------+\r\n\r\n Choose 1-4  (Enter = desktop): "
