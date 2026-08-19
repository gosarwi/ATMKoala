# ATMKoala v0.9: Limine-only ISO boot path

## Статус

**Limine v12.6.0 является единственным активным ISO bootloader ATMKoala.** Основной build больше не создаёт GRUB staging tree и не использует `grub-mkrescue`. Замороженные `ATMBOOT` и `ATMUEFI` сохраняются как независимые legacy boot paths, но не являются частью ISO workflow.

| Задача | Команда | Результат |
|---|---|---|
| Собрать kernel | `make all` | `build/kernel.bin` |
| Собрать bootable ISO | `make iso` | `atmkoala-OS-v0.9-limine.iso` |
| Явно собрать Limine ISO | `make limine` или `make limine-iso` | Тот же ISO artifact |
| BIOS QEMU | `make run` или `make run-limine` | Limine BIOS menu → Multiboot2 kernel |
| BIOS QEMU with VBE | `make run-vbe` | Limine ISO with standard VBE device |

## Handoff contract

Limine использует `protocol: multiboot2` для существующего kernel image. Это оставляет ABI ранней загрузки неизменным: `boot/boot.s` получает Multiboot2 magic в `EAX` и physical pointer на information structure в `EBX`, затем сам переключает CPU в long mode и вызывает kernel entry. Следовательно, миграция ISO bootloader не меняет paging, user ABI, VFS или Exp.

| Firmware | ISO mechanism | Kernel/config location |
|---|---|---|
| BIOS | Limine BIOS CD stage + `limine-bios.sys` | ISO root config `/limine.conf`; kernel `/boot/kernel.bin` |
| UEFI | Generated FAT12 EFI El Torito image, linked as EFI boot partition | `BOOTX64.EFI`, UEFI-local `/limine.conf`, kernel `/kernel.bin` on one FAT volume |

Параметр `-partition_offset 16` резервирует 32 KiB до ISO partition start. Это позволяет local `limine bios-install` корректно устанавливать BIOS stages в generated ISO без обращения к пользовательским дискам. ISO build также содержит `BOOTX64.EFI` и a GPT-linked EFI boot image for OVMF.

## Меню

BIOS configuration предоставляет graphical profiles `800×600`, `1024×768`, `640×480`, Text Mode и Disk Installer. UEFI FAT volume предоставляет `800×600` и Text Mode. Installer entry явно отмечает destructive nature и передаёт kernel argument `installer`.

## Проверка

Regression выполняется в QEMU для обоих firmware paths. Успешный результат должен содержать Limine `multiboot2: Loading executable`, затем kernel VBE initialization `VBE OK`. Static CPL3 libc smoke и normal kernel tests остаются частью kernel image и не зависят от Limine.

## Sources

[1] [Limine v12 configuration](https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md)

[2] [Limine hybrid ISO usage](https://github.com/Limine-Bootloader/Limine/blob/v12.x/USAGE.md)

[3] [ATMKoala Limine vendor record](third_party/limine/ATMKOALA_VENDOR.md)
