# Проверка cfdisk и расширенных bounded-команд

**Дата:** 22 августа 2026 года
**Область:** активная Limine hybrid ISO ветка ATMKoala. Этот пакет **не опубликован** в GitHub.

## Реализованные функции

| Компонент | Проверяемое поведение | Намеренная граница |
|---|---|---|
| `atm-box crc32` | Потоково вычисляет CRC-32/ISO-HDLC для VFS-файла, печатает 32-bit value и число просмотренных байт. | Сканирование ограничено 16 MiB; CRC32 не является криптографическим хешем или проверкой подписи. |
| `atm-box strings` | Находит printable ASCII runs в VFS-файле; `-n` принимает 3..32. | 64 KiB scan и 128 строк; нет Unicode, regex, pipes или неограниченного вывода. |
| `atm-box seq` | Печатает ограниченную signed-decimal последовательность с проверкой нулевого шага и диапазона. | Не более 512 строк; это не shell language или pipeline producer. |
| `cfdisk` / `fdisk` / `diskmgr` | Общий VGA text staged MBR editor: выбор уже найденного ATA drive, add/delete/type/boot/reload, preview dirty table, `WRITE` confirmation, MBR write и re-read comparison. | Только до 4 primary MBR entries; нет GPT, extended/logical partitions, resize/move FS, automatic format, bootloader install или arbitrary device access. |
| CatFS format in cfdisk | Доступен только для selected CatFS entry после чистого/committed table и отдельного `FORMAT` token. | Не форматирует автоматически после add/write и не поддерживает other filesystem formatters. |

## Подтверждение destructive границы

Изменения таблицы остаются в памяти, пока `W` не встретит успешную `mbr_validate_drive()` и пользователь не введёт английское слово `WRITE` латинскими буквами (регистр нормализуется). Отмена выхода/reload из dirty state требует `DISCARD`. Операция формата требует независимое `FORMAT`. Состояние write проверяется повторным чтением MBR и byte-for-byte сравнением всех четырёх entries.

## Регрессии

| Команда | Результат | Повторы |
|---|---|---:|
| `make all` | Успешно | Многократно во время интеграции; финально перед ISO. |
| `make atmloader` | Успешно; создан Limine hybrid ISO. | 1 |
| `bash tests_qemu_cfdisk_commit.sh` | Успешно: fresh 64 MiB disposable image, text boot, `cfdisk`, staged 32 MiB CatFS entry at LBA 2048, typed `WRITE`, serial verified re-read and host raw-MBR byte checks. | 2 |
| `bash tests_qemu_linux_l0.sh` | Успешно, включая обязательные `[atmbox] primitives-ok` и `[cfdisk] staged-ok`. | 2 |
| `bash tests_qemu_ext2_write_type.sh` | Успешно; existing guarded direct-block Ext2 write and external persistence verification. | 2 |

## Итоговые артефакты

| Артефакт | SHA-256 |
|---|---|
| `build/kernel.bin` | `87834905da9994dfc88d1f6d7e136e5f12850eab0a57b9448d4a87b9bccb75f3` |
| `atmkoala-OS-v0.9-limine.iso` | `074657313379501c15cd7033464098ca27802eb772e699caedb5a43b86856249` |

Проверки подтверждают bounded command primitives, staged MBR state transitions и один реальный commit только на freshly generated disposable QEMU image. Они не подтверждают безопасную разметку пользовательского оборудования без внимания пользователя, filesystem resize/repair, GPT support, full POSIX или Linux binary compatibility.

Связанная подробная матрица: [`POSIX_COMPATIBILITY_STATUS.md`](POSIX_COMPATIBILITY_STATUS.md).
