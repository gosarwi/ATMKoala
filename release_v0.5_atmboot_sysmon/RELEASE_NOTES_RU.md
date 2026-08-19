# ATMKoala v0.5 — ATMBOOT, System Monitor и стабилизация

**Архитектура:** freestanding x86-64, C + ASM, без libc.  
**Проверка:** QEMU, 256 MiB RAM, VBE, IDE raw disk.

> Эта сборка добавляет собственную BIOS-загрузочную цепочку **ATMBOOT** как отдельный raw-образ и сохраняет проверенный GRUB ISO как fallback/recovery path.

## Результат обновления

| Область | Реализовано |
|---|---|
| **Собственный bootloader** | ATMBOOT stage 0/2 без GRUB: BIOS MBR → INT 13h Extensions → stage 2 → ATA PIO LBA28 → flat kernel payload. |
| **Совместимый запуск ядра** | Stage 2 формирует минимальный Multiboot2-compatible framebuffer handoff; ядро использует прежний проверенный x86-64 entry и VBE path. |
| **VBE** | Stage 2 запрашивает VBE mode `0x118`; при невозможности перейти в VBE ядро получает text-mode fallback context. |
| **System Monitor** | Случайная CPU заглушка удалена. CPU рассчитывается по delta idle ticks scheduler; RAM — по реальному heap; Disk I/O — по ATA read/write counters. |
| **Диски** | ATA IDENTIFY извлекает model, serial, firmware, LBA48 availability и total capacity. Monitor показывает model, MiB и LBA mode. |
| **Maze panic** | `maze` удалён из shell dispatcher, game launcher, store catalog и kernel link list. Команда безопасно отвечает `command not found`. |
| **Фирменный стиль** | Предоставленные PNG и ASCII reference assets сохранены с SHA-256. В Exp About добавлен компактный Q-koala mark; ATMBOOT показывает Q-koala splash. |

## ATMBOOT

ATMBOOT является самостоятельным BIOS/IDE boot target. Он не загружает GRUB, не читает filesystem и не заменяет UEFI loader. Это намеренно компактный и проверяемый первый этап собственного загрузчика.

| Часть | Расположение | Задача |
|---|---:|---|
| Stage 0 | MBR, LBA 0, `0x7C00` | Получает boot drive и читает 64 sectors stage 2 через INT 13h EDD. |
| Stage 2 | LBA 1–64, `0x8000` | Splash, VBE, protected mode, ATA PIO загрузка flat payload. |
| Kernel payload | LBA 65+, `0x00200000` | `objcopy -O binary` образ, что сохраняет link-time VMA layout. |
| Kernel handoff | `_start` at `0x00201000` | `EAX=Multiboot2 magic`, `EBX=minimal info at 0x5000`. |

> В раннем тесте был обнаружен и исправлен важный дефект: нельзя передавать ELF container как raw payload. ELF file headers сдвигали текст относительно `_start`. Теперь `make atmboot` создаёт flat binary через `objcopy -O binary`, и независимый BIOS boot проходит в QEMU.

### Сборка и запуск ATMBOOT

```bash
make atmboot
qemu-system-x86_64 \
  -drive file=atmkoala-atmboot.img,format=raw,if=ide \
  -boot c -m 256M -vga std -no-reboot
```

Для GRUB ISO сохраняется стандартный путь:

```bash
qemu-system-x86_64 \
  -cdrom atmkoala-OS-v0.5.iso \
  -m 256M -vga std -no-reboot
```

## System Monitor и распознавание дисков

System Monitor больше не формирует CPU usage LCG-генератором. Значение CPU равно доле scheduler ticks, не проведённых idle task, за последний sampling interval. Disk I/O graph отражает число ATA read/write операций в interval. В окне также показаны uptime, task count, heap free, количество ATA devices и counters ошибок.

При ATA IDENTIFY драйвер нормализует swapped strings и сохраняет:

| Поле | Использование |
|---|---|
| Model | Отображается в System Monitor и `disk` info. |
| Serial / firmware | Сохранены в `disk_drive_t` для shell/diagnostic consumers. |
| LBA28 sectors | Текущая безопасная PIO addressing boundary. |
| LBA48 / total sectors | Детектируются и показываются в UI; full LBA48 I/O commands не заявлены как готовые. |

QEMU regression с raw IDE image 128 MiB обнаружила устройство как `hda QEMU HARDDISK 128 MiB LBA48`.

## Maze

Maze Explorer удалён из поставляемого kernel, поскольку его входной путь воспроизводимо приводил к kernel panic. Он удалён не только из команды `maze`, но и из game launcher, store catalog и link list. Это исключает обход через другой UI path.

## Логотип и assets

Оригинальные пользовательские reference files включены в `branding/` без изменения:

| Файл | Назначение |
|---|---|
| `branding/atmkoala-logo-reference.png` | Исходный графический логотип. |
| `branding/atmkoala-logo-ascii.txt` | Предоставленная ASCII-вариация. |
| `branding/SHA256SUMS` | Контрольные суммы этих source assets. |

## QEMU regression

| Сценарий | Результат | Evidence |
|---|---|---|
| ATMBOOT raw BIOS boot без CD-ROM/GRUB | Пройден, Exp доступен; serial содержит `[vbe] OK`. | `screenshots/atmboot-final-exp.png` |
| System Monitor + 128 MiB IDE disk | Пройден: CPU/RAM/I-O graphs и `hda ... 128 MiB LBA48`. | `screenshots/system-monitor-ata.png` |
| `maze` из text shell | Пройден: command-not-found, panic отсутствует. | `screenshots/maze-removed.png` |
| Exp About + Q-koala mark | Пройден на default UI scale 130%. | `screenshots/exp-q-koala-about.png` |

## Состав release

| Файл | Содержание |
|---|---|
| `ATMKoala-v0.5-ATMBOOT-Exp.iso` | Загрузочный GRUB ISO с полным Exp desktop и fallback entries. |
| `ATMKoala-v0.5-ATMBOOT-BIOS.raw` | Отдельный raw BIOS/IDE ATMBOOT image без GRUB. |
| `ATMKoala-v0.5-ATMBOOT-Exp-src.tar.gz` | Исходный архив этой конкретной сборки. |
| `ATMBOOT_DESIGN.md` | Технический контракт stage 0/2 и boot handoff. |
| `QA_ATMBOOT_NOTES.md` | Хронология обнаруженных и исправленных проблем. |

