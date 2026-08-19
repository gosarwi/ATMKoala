# ATMBOOT: собственная загрузочная цепочка ATMKoala

## Назначение

ATMBOOT — минимальная BIOS/MBR загрузочная цепочка для ATMKoala. Она дополняет, а не немедленно заменяет GRUB: обычный ISO продолжает использовать проверенный Multiboot2 path и сохраняет все GRUB entries, включая текстовый режим и отдельный GUI installer. Собственный загрузчик выпускается как отдельный raw-образ для QEMU/BIOS и является основой для дальнейшей установки на диск.

| Компонент | Адрес или LBA | Роль |
|---|---:|---|
| Stage 0 | BIOS `0x7C00`, LBA 0 | MBR-совместимый сектор, загружает stage 2 через INT 13h extensions. |
| Stage 2 | `0x8000`, LBA 1–64 | Показывает ASCII splash, выставляет VBE mode 0x118 и формирует минимальную Multiboot2-совместимую framebuffer tag. |
| Kernel raw payload | `0x00200000`, LBA 65 | Плоский образ текущего kernel ELF, читается собственным ATA PIO LBA28 loader. |
| BSS/page-table clear | `0x00300000–0x01000000` | Обнуляется до передачи управления существующему 32→64-bit entry. |

## Совместимый handoff

Stage 2 передаёт в существующий `_start` `EAX=0x36D76289` и `EBX=0x5000`. В `0x5000` размещается корректно выровненная минимальная Multiboot2 information structure с framebuffer tag. Благодаря этому текущий `kernel_main()` инициализирует VBE без специальной ветки только для ATMBOOT.

Такой подход сохраняет проверенный long-mode entry в `boot/boot.s`: ATMBOOT отвечает за BIOS, VBE и загрузку raw payload, а ядро сохраняет один путь настройки page tables, GDT, IDT и последующей инициализации.

## Ограничения первой версии

Первый ATMBOOT target предназначен для BIOS/QEMU с IDE primary master и VBE mode 0x118. Он не заменяет UEFI loader, не является installer и не интерпретирует filesystem или partition layout. Это сознательно отдельная тестируемая ступень: MBR partition table не переписывается существующим partition manager, а GRUB ISO остаётся recovery/fallback path.

## Сборка и запуск

```bash
make atmboot
qemu-system-x86_64 -drive file=atmkoala-atmboot.img,format=raw,if=ide -m 256M -vga std -no-reboot
```

