# ATMBOOT QEMU QA

The first ATMBOOT image produced a black screen and shutdown because stage 2 copied the ELF container directly to physical 2 MiB. The existing kernel entry is at VMA `0x201000`, while ELF file headers shifted the text bytes in a raw disk copy. The raw image builder was corrected to use `objcopy -O binary`, which preserves the linker's VMA-relative flat image layout.

The corrected BIOS/IDE image was booted in QEMU with `-boot c` and no CD-ROM/GRUB. Stage 0 loaded stage 2 through BIOS EDD, stage 2 installed VBE mode 0x118, loaded the flat payload through ATA PIO LBA28, built the compatible framebuffer information block, and the existing kernel reached Exp successfully. The QEMU serial log reported `vbe ... 1024x768` and `OK`; the framebuffer capture showed the interactive Exp terminal.


## System Monitor and ATA QA

A QEMU regression with a 128 MiB IDE image detected `hda QEMU HARDDISK 128 MiB LBA48`. System Monitor now renders real scheduler idle-derived CPU utilization, heap ratio, ATA I/O operation history, actual read/write/error counters, and the detected model/capacity/LBA mode. A first visual pass found unsupported custom formatter tokens; the panel now builds text with kernel string helpers and renders correctly.


## Maze removal and branding QA

Fresh-QEMU shell invocation of `maze` now returns `command not found (type 'help')`; `game_maze` is absent from the linked kernel, and Maze is removed from launcher and store catalog. The Exp About view renders the new compact Q-koala mark and product-card branding at 130% UI scale. The original user-supplied PNG and ASCII source are preserved under `assets/branding/` with checksums; ATMBOOT also renders a compatible textual Q-koala splash before loading the kernel.


## Final boot regression

After the final full build, `make atmboot` produced a 557,568-byte raw BIOS/IDE image. A fresh QEMU boot from this image (without CD-ROM or GRUB) reached the Exp terminal and the serial log contained `[vbe] OK`.


## ATMUEFI media discovery

The initial FAT superfloppy and subsequent GPT+EF00 ESP both contain the required `\\EFI\\BOOT\\BOOTX64.EFI` and `\\EFI\\ATMKOALA\\KERNEL.BIN`. OVMF did not automatically select the virtio drive's removable path in this environment, even with the standard GPT ESP. The next regression attaches the same image through an OVMF-recognized SATA/USB controller; the ESP content itself is verified with mtools.


## ATMUEFI OVMF regression

ATMUEFI now builds a PE32+ `EFI application` and a GPT ESP with `\\EFI\\BOOT\\BOOTX64.EFI` plus `\\EFI\\ATMKOALA\\KERNEL.BIN`. The native OVMF test uses the image as removable USB storage. The loader detects GOP at 1280x800, reads the flat kernel payload through UEFI Simple File System, exits boot services and hands control to a direct 64-bit kernel entry.

Several boot blockers were resolved during QA: the PE subsystem had initially been unspecified; direct Boot Services calls required the GNU-EFI MS x64 ABI; and a first 64-bit entry retained OVMF's CS selector, causing a GP on interrupt return. The final entry reloads the kernel GDT code selector before IDT use. The successful OVMF serial log reports `GOP 1280x800 ready`, `[vbe] ... 1280 h=800`, and `[vbe] OK`; the framebuffer capture shows the interactive Exp terminal.


## Cross-path relocation regression

The kernel's fixed physical base is now 64 MiB, with heap at 128 MiB, avoiding UEFI low-memory allocations. Fresh QEMU checks verified all shipped paths after this relocation: (1) OVMF + removable GPT ESP loads ATMUEFI and reaches Exp at 1280x800; (2) BIOS raw ATMBOOT reaches Exp at 1024x768 with VBE OK; and (3) the rebuilt GRUB ISO fallback reaches Exp at 800x600 with VBE OK.


## ATM Loader BIOS grid

The BIOS ATM Loader grid renders before VBE setup and correctly labels the shared options. QEMU regression selected tile 1 and reached Exp with VBE OK. A fresh regression selected tile 2 and reached the kernel's text console; the serial log contained no VBE initialization, confirming that the standard `novbe` Multiboot2 cmdline handoff is real rather than cosmetic.


## ATM Loader UEFI grid

OVMF renders the same 2×2 ATM Loader selection contract with the UEFI/ATMUEFI status line. Tile 1 was selected and reached Exp with VBE OK. A fresh tile-2 selection emitted no GOP/VBE initialization in the serial log, confirming the `novbe` handoff. On OVMF's GOP console, legacy VGA text is not necessarily redrawn into the prior UEFI framebuffer; this is expected for the text-safe compatibility branch and does not affect BIOS text output.


Tile 3 on ATMUEFI was exercised on the final rebuilt ESP: the kernel reported GOP/VBE OK and entered the dedicated installer dispatch path. A first capture exposed intermediate redraw tearing on the GOP framebuffer; the installer now enables the existing VBE double buffer before its redraw loop. The fresh final OVMF capture shows the complete stable welcome card. The same fresh tile-3 regression was run on the final ATMBOOT raw image with VBE OK and the complete 1024×768 welcome card. The graphical installer route is therefore verified in both firmware paths.

## Release capture review

The BIOS release capture shows the four-cell ATM Loader grid exactly as specified. The desktop capture after tile 1 shows Exp running with its terminal and taskbar at 1024×768.



## v0.9 foundation baseline — 2026-08-19

После расширения POSIX/ABI foundation rebuilt ISO, ATMBOOT и ATMUEFI images используют один kernel image. На GRUB ISO, BIOS ATMBOOT tile 1 и UEFI ATMUEFI tile 1 успешно выполнена `posix test`: `paging=OK`, `uaccess=OK`, `vfs-posix=OK`, `syscall-usercopy=OK`, `process-fd=OK`, `native-cpl3=OK`. BIOS path confirmed VBE `1024×768×24`; UEFI path confirmed GOP `1280×800` and VBE handoff. Это покрывает task-local native FDs, bounded process `brk`, static ELF64 → CPL 3 → `exit(42)` → `waitpid` lifecycle и отсутствие regression unified ATM Loader menu.
