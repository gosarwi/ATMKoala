# ATMBOOT QEMU QA

The first ATMBOOT image produced a black screen and shutdown because stage 2 copied the ELF container directly to physical 2 MiB. The existing kernel entry is at VMA `0x201000`, while ELF file headers shifted the text bytes in a raw disk copy. The raw image builder was corrected to use `objcopy -O binary`, which preserves the linker's VMA-relative flat image layout.

The corrected BIOS/IDE image was booted in QEMU with `-boot c` and no CD-ROM/GRUB. Stage 0 loaded stage 2 through BIOS EDD, stage 2 installed VBE mode 0x118, loaded the flat payload through ATA PIO LBA28, built the compatible framebuffer information block, and the existing kernel reached Exp successfully. The QEMU serial log reported `vbe ... 1024x768` and `OK`; the framebuffer capture showed the interactive Exp terminal.


## System Monitor and ATA QA

A QEMU regression with a 128 MiB IDE image detected `hda QEMU HARDDISK 128 MiB LBA48`. System Monitor now renders real scheduler idle-derived CPU utilization, heap ratio, ATA I/O operation history, actual read/write/error counters, and the detected model/capacity/LBA mode. A first visual pass found unsupported custom formatter tokens; the panel now builds text with kernel string helpers and renders correctly.


## Maze removal and branding QA

Fresh-QEMU shell invocation of `maze` now returns `command not found (type 'help')`; `game_maze` is absent from the linked kernel, and Maze is removed from launcher and store catalog. The Exp About view renders the new compact Q-koala mark and product-card branding at 130% UI scale. The original user-supplied PNG and ASCII source are preserved under `assets/branding/` with checksums; ATMBOOT also renders a compatible textual Q-koala splash before loading the kernel.


## Final boot regression

After the final full build, `make atmboot` produced a 557,568-byte raw BIOS/IDE image. A fresh QEMU boot from this image (without CD-ROM or GRUB) reached the Exp terminal and the serial log contained `[vbe] OK`.

