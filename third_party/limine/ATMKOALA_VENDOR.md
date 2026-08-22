# Limine vendor record

ATMKoala vendors the official Limine **v12.6.0** binary release for the **primary hybrid ISO path** of the unified **ATM Loader** project. ATMBOOT and ATMUEFI are maintained BIOS and UEFI fallback/development paths; they are not replaced by Limine source and remain separate firmware executables.

| Field | Value |
|---|---|
| Upstream | [`Limine-Bootloader/Limine`](https://github.com/Limine-Bootloader/Limine) |
| Release | [`v12.6.0`](https://github.com/Limine-Bootloader/Limine/releases/tag/v12.6.0) |
| Primary protocol used by ATMKoala | Multiboot2 |
| License | BSD-2-Clause; see `bin/LICENSE` |
| Archive | `limine-binary.tar.gz` |
| SHA-256 | `8edf447b9c3c9bbd55b1e1e43528289ccb9fc8cb6f6f9edb4de3b7a2380671fe` |
| Included build files | BIOS CD image, BIOS stage file, `BOOTX64.EFI`, and local `limine` host tool source |

The `limine` host tool is compiled locally from the release-provided `limine.c` and is used only to run `bios-install` on the generated hybrid ISO. The build creates its UEFI El Torito image as a new FAT12 volume containing `BOOTX64.EFI`, `limine.conf`, and the kernel; the vendor UEFI CD blob is not modified. The kernel is unchanged at the ABI level: Limine launches its existing Multiboot2 header and the kernel continues to receive the Multiboot2 magic and information pointer expected by `boot/boot.s` and `src/kernel.c`.

## Unified project boundary

`make atmloader` creates three artifacts under the common menu contract in `boot/atmloader/MENU_CONTRACT.md`:

| Artifact | Role | Verified QEMU evidence |
|---|---|---|
| `atmkoala-OS-v0.9-limine.iso` | Primary Limine BIOS/UEFI hybrid distribution | Full BIOS kernel regression plus an OVMF trace through Limine UEFI menu and Multiboot2 framebuffer handoff. |
| `atmkoala-atmboot.img` | ATMBOOT BIOS/IDE fallback | QEMU text-safe menu selection reaches the kernel console. The graphical VBE path is not promoted to full-regression status. |
| `atmkoala-atmuefi.img` | ATMUEFI UEFI ESP fallback | OVMF text-safe menu selection reaches the kernel console. |

The unified project shares visible intent and Multiboot2 handoff semantics; it does **not** claim that BIOS 16-bit MBR code, UEFI PE/COFF code, and Limine are one binary or have identical graphics-mode support. Limine attribution and disclaimers remain in the bundled `bin/LICENSE` as required by its BSD-2-Clause terms.
