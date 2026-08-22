# Проверка пакета Exp Desktop Tools

**Дата:** 22 августа 2026 года
**Область:** активная Limine hybrid ISO ветка ATMKoala. Публикация в GitHub для этого пакета **не выполнялась**.

## Реализованный пакет

| Возможность | Проверяемое поведение | Намеренное ограничение |
|---|---|---|
| Command Palette | `Alt+R` открывает bounded literal filter по 30 встроенным Exp-приложениям. Стрелки выбирают, Enter открывает существующее native приложение. | Не выполняет shell-команды, не ищет файлы, не загружает внешние приложения. |
| Window Overview | `Ctrl+W` показывает до восьми текущих окон; Enter восстанавливает выбранное и переносит его вперёд. | Это не виртуальные рабочие столы и не диспетчер процессов. |
| Show Desktop | `Ctrl+D` или glyph taskbar временно сворачивает только окна, которые были видимы в момент действия; следующее переключение восстанавливает именно этот набор. | Не сохраняется между сессиями и не меняет ранее свёрнутые окна. |
| PPM to Paint | В Files клавиша `O` передаёт только выбранный PPM P6 в новый Paint. Изображение декодируется существующим bounded decoder и nearest-sampled в 32×18, один Undo восстанавливает прежнее поле. | Нет PNG/JPEG/P3/16-bit import, палитра ограничена восьмью текущими цветами, нет произвольного canvas size. |
| VBE visibility | System Information и явный Diagnostic export сообщают принятую VBE геометрию и present path: reserved, heap или direct fallback. | Это read-only telemetry; она не делает modesetting, EDID probing и не гарантирует производительность GPU. |

## Дополнительные pure проверки

`exp_text_layout_selftest()` теперь дополнительно проверяет case-insensitive literal matching command palette, отказ несуществующего фильтра, PPM format routing и exact palette mapping для цвета `C_RED`. Эти проверки не открывают окно, не создают файл и не выполняют I/O.

## Итоговая сборка и артефакты

| Артефакт | SHA-256 |
|---|---|
| `build/kernel.bin` | `a03cd74dbd86b5eadbb54fa98530416c03a422cf034e9b64d7ec50ba608e655f` |
| `atmkoala-OS-v0.9-limine.iso` | `c25670b17a7612e81049e3becd5f5783c569600b60131995fc2a905a87e6fb66` |

## Выполненные проверки

| Команда | Результат | Повторы |
|---|---|---:|
| `make all` | Успешно | Многократно во время интеграции; финально перед ISO. |
| `make atmloader` | Успешно; создан Limine hybrid ISO | 1 |
| `bash tests_qemu_linux_l0.sh` | Успешно; включает graphical boot splash, Exp layout aggregate, VBE geometry/backbuffer markers, mouse, timezone/TZif, network/parser и ABI regression checks. | 2 |
| `bash tests_qemu_ext2_write_type.sh` | Успешно; generated MBR+Ext2 image, guarded existing direct-block write и external persistence verification. | 2 |

Эти результаты подтверждают встроенные bounded selftests и QEMU-маршруты. Они не заявляют полный POSIX, arbitrary Linux binary compatibility, runtime GPU modesetting, universal input-driver coverage, полный image editor или unrestricted filesystem mutation.

## Связанная документация

Подробная матрица возможностей и границ: [`POSIX_COMPATIBILITY_STATUS.md`](POSIX_COMPATIBILITY_STATUS.md).
