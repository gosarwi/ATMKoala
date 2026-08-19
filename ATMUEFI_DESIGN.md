# ATMUEFI — UEFI branch of ATMBOOT

## Scope

ATMUEFI is a standalone x86-64 UEFI application installed as `\EFI\BOOT\BOOTX64.EFI` on a FAT EFI System Partition (ESP). It complements the existing BIOS ATMBOOT raw image and the GRUB ISO. No existing boot path is removed.

| Boot path | Firmware | Artifact | Status |
|---|---|---|---|
| ATMBOOT BIOS | Legacy BIOS / IDE | `atmkoala-atmboot.img` | Stage 0 + stage 2, ATA PIO raw payload. |
| ATMUEFI | UEFI x86-64 | `atmkoala-atmuefi.img` | GOP framebuffer, FAT ESP, native UEFI file loading. |
| GRUB ISO | BIOS or UEFI capable GRUB environment | `atmkoala-OS-v0.5.iso` | Recovery/fallback path. |

## Handoff contract

The existing kernel already accepts a Multiboot2-style information block with a framebuffer tag. ATMUEFI builds that same minimal block in low conventional memory using `AllocatePages(AllocateMaxAddress)`, passes the existing Multiboot2 boot magic in `EAX`, and passes the information pointer in `EBX` to the kernel x86 entry.

The loader reads `\EFI\ATMKOALA\KERNEL.BIN`, a flat `objcopy -O binary` kernel image, and reserves physical memory from `0x00200000` through `0x00600000`. This accommodates the current code/data payload, page tables and BSS, whose addresses remain fixed by the existing linker script. It uses the GOP framebuffer descriptor to populate width, height, pitch, format and physical framebuffer address.

## UEFI stages

| Stage | Operation |
|---|---|
| Firmware application | Open the loaded-image device's Simple File System, open the flat kernel file, and draw a small Q-koala textual splash. |
| Memory preparation | Reserve the kernel's fixed physical region and a low page for the information block. |
| Graphics | Locate GOP and emit a 32-bit RGB-compatible framebuffer tag. |
| Ownership transfer | Obtain final memory-map key, close files, `ExitBootServices`, disable firmware paging through a dedicated assembly transition, then jump to the established 32-bit `_start` at `0x00201000`. |

## Constraints of this increment

ATMUEFI targets x86-64 UEFI with GOP and conventional memory available at the current fixed kernel addresses. It is not yet Secure Boot signed, does not implement UEFI runtime services after `ExitBootServices`, and does not yet contain a generic ELF loader or filesystem driver in the kernel. Those are future extensions; this loader is deliberately focused on a reproducible native FAT-ESP boot path.

## Reproducible test

```bash
make atmuefi
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/ATMKOALA_VARS.fd
qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/ATMKOALA_VARS.fd \
  -drive file=atmkoala-atmuefi.img,format=raw,if=virtio \
  -m 256M -vga std -no-reboot
```
