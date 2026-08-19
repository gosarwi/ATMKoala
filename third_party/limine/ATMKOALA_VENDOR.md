# Limine vendor record

ATMKoala vendors the official Limine **v12.6.0** binary release for its **sole active ISO boot path**. GRUB staging/configuration is not part of the main build. The frozen `boot/atmboot` and `boot/atmuefi` directories are not modified.

| Field | Value |
|---|---|
| Upstream | [`Limine-Bootloader/Limine`](https://github.com/Limine-Bootloader/Limine) |
| Release | [`v12.6.0`](https://github.com/Limine-Bootloader/Limine/releases/tag/v12.6.0) |
| Protocol used by ATMKoala | Multiboot2 |
| License | BSD-2-Clause (upstream `LICENSE`) |
| Archive | `limine-binary.tar.gz` |
| SHA-256 | `8edf447b9c3c9bbd55b1e1e43528289ccb9fc8cb6f6f9edb4de3b7a2380671fe` |
| Included build files | BIOS CD image, BIOS stage file, `BOOTX64.EFI`, and local `limine` host tool source |

The `limine` host tool is compiled locally from the release-provided `limine.c` and is used only to run `bios-install` on the generated hybrid ISO. The build creates its UEFI El Torito image as a new FAT12 volume containing `BOOTX64.EFI`, `limine.conf`, and the kernel; the vendor UEFI CD blob is not modified. The kernel is unchanged at the ABI level: Limine launches its existing Multiboot2 header and the kernel continues to receive the Multiboot2 magic and information pointer expected by `boot/boot.s` and `src/kernel.c`.
