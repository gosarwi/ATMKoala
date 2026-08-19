# ATMKoala v0.5 — финальный релиз ATM Loader

**Дата сборки:** 19 августа 2026 г.  
**Целевая архитектура:** x86-64, freestanding C + ASM, без libc.

## Что изменилось

Этот релиз завершает объединение собственных загрузчиков **ATMBOOT** для классического BIOS и **ATMUEFI** для UEFI в единый пользовательский контракт **ATM Loader**. В обеих ветках перед стартом ядра отображается одинаковая текстовая сетка 2×2. Выбор передаётся ядру как Multiboot2 command-line tag, поэтому BIOS и UEFI используют одни и те же режимы работы, а не две расходящиеся реализации.

| Клавиша | Режим | Передаваемый аргумент | Результат |
|---|---|---|---|
| `1` или `Enter` | **EXP DESKTOP** | отсутствует | Графический рабочий стол Exp с VBE/GOP framebuffer. |
| `2` | **COMPAT CONSOLE** | `novbe` | Текстовый безопасный режим без инициализации VBE. |
| `3` | **DISK INSTALLER** | `installer` | Отдельный графический установщик CatFS; доступен только через меню загрузчика. |
| `4` | **RESTART** | — | Перезапуск через firmware: INT 19h на BIOS, ResetSystem на UEFI. |

> В режиме `installer` форматируется **ровно один выбранный ATA-диск** в CatFS. Установщик не создаёт таблицу разделов, не устанавливает bootloader, не выполняет перенос Btrfs и не восстанавливает данные. Перед подтверждением операции необходимо отсоединить важные носители.

## Состав релиза

| Файл | Назначение | Как запускать |
|---|---|---|
| `atmkoala-OS-v0.5.iso` | GRUB ISO fallback с пятью входами, включая installer. | QEMU/реальный BIOS или UEFI через ISO. |
| `atmkoala-atmboot.img` | Самостоятельный BIOS raw IDE образ с MBR stage 0 + stage 2 ATM Loader. | Загрузка с IDE/raw-диска на BIOS. |
| `atmkoala-atmuefi.img` | 64 MiB GPT-образ с FAT32 ESP, `BOOTX64.EFI` и единым ядром. | UEFI x86-64, в QEMU/OVMF — как USB/ESP носитель. |
| `ATMKoala-v0.5-ATMLoader-src.tar.gz` | Воспроизводимый архив исходного дерева, документации и ресурсов. | Распаковать и собрать командами ниже. |

Ядро релиза размещается с физической базы **64 MiB**. BIOS entry находится по `0x04001000`, а прямой 64-битный UEFI entry — по `0x04001158`; heap начинается с 128 MiB. Такое размещение устраняет конфликт с низкой памятью, выделяемой прошивкой UEFI.

## QA-регрессия

Собранный комплект прошёл проверку в QEMU. BIOS raw образ загружается без GRUB, отображает сетку ATM Loader, запускает Exp в tile 1, отключает VBE для `novbe` в tile 2 и выводит полный экран приветствия Disk Installer в tile 3. UEFI GPT ESP образ проверен в OVMF с removable USB storage: GOP 1280×800 передаётся ядру, tile 1 запускает Exp, tile 2 передаёт `novbe`, а tile 3 открывает стабильную карточку установщика.

Во время последней регрессии был устранён артефакт неполной прорисовки первого кадра installer на UEFI GOP. Теперь `installer_run()` включает штатный двойной VBE buffer до цикла перерисовки и освобождает его при выходе из программы. Поэтому загрузочный tile 3 одинаково стабильно отображается по BIOS и UEFI.

Полный журнал проверок находится в `QA_ATMBOOT_NOTES.md`. Каталог `screenshots/` содержит снимки BIOS и UEFI сетки, рабочего стола, режима совместимой консоли и установщика.

## Сборка из исходников

```bash
cd QewoxOS1
make all && make iso && make atmboot && make atmuefi
```

В `Makefile` есть guard, который перед созданием UEFI образа проверяет адрес `uefi_start = 0x0000000004001158`.

## Запуск в QEMU

### GRUB ISO

```bash
qemu-system-x86_64 \
  -m 256M \
  -cdrom atmkoala-OS-v0.5.iso \
  -boot d \
  -vga std
```

### BIOS ATMBOOT

```bash
qemu-system-x86_64 \
  -m 256M \
  -drive file=atmkoala-atmboot.img,format=raw,if=ide \
  -boot c \
  -vga std
```

### UEFI ATMUEFI в OVMF

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd OVMF_VARS_ATMKOALA.fd
qemu-system-x86_64 \
  -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS_ATMKOALA.fd \
  -drive if=none,id=esp,file=atmkoala-atmuefi.img,format=raw \
  -device qemu-xhci,id=xhci \
  -device usb-storage,drive=esp \
  -vga std
```

На экране ATM Loader нажмите `1`, `2`, `3` или `4`; `Enter` эквивалентен tile 1.

## Проверка целостности

Файл `SHA256SUMS` содержит контрольные суммы всех главных артефактов релиза. Перед запуском проверьте их командой:

```bash
sha256sum -c SHA256SUMS
```

## Известные границы

ATMKoala остаётся экспериментальной ОС. Набор POSIX-функций является переносимым подмножеством, а установщик намеренно выполняет ограниченную операцию форматирования CatFS в live-сессии. Для production-данных и реальных рабочих систем этот релиз не предназначен.
