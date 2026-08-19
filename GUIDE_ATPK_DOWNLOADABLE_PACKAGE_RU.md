# Как создать скачиваемый пакет ATPK для ATMKoala

**Автор:** Manus AI  
**Статус:** практическое руководство для ATMKoala v0.5  
**Формат:** `.atpk` — нативный ATMKoala package, не `.deb`

## 1. Что такое ATPK

ATPK — это безопасный нативный формат приложений ATMKoala. По общей идее он похож на Debian-пакет: метаданные и список файлов отделены от данных, а установка выполняется через проверку и staging. Однако ATPK не является `.deb`, не использует `dpkg`, не запускает maintainer scripts и не принимает Linux ELF как нативное ATMKoala-приложение.

Рекомендуемый layout скачиваемого файла:

```text
weather.atpk
├── ATPK/control       # имя, версия, архитектура, зависимости, описание
├── ATPK/manifest      # путь, размер и CRC32 каждого файла
├── ATPK/hooks         # только декларативные действия, без shell-кода
└── data/
    ├── apps/weather/weather.elf
    ├── share/icons/weather.psf
    └── share/doc/weather/README.md
```

Контейнер использует tar/zstd-представление, но контрольные записи ATPK должны находиться по безопасным относительным путям. Нельзя помещать в архив `/etc/...`, `../...`, duplicate paths, device nodes или произвольные install scripts.

## 2. Что должно быть в ATPK/control

Пример control-файла:

```text
Package: weather
Version: 1.0.0
Architecture: atmkoala-x86_64
Depends: exp>=0.5
Maintainer: ATMKoala Example Team
Description: Демонстрационное окно погоды для Exp
Entry: apps/weather/weather.elf
License: MIT
```

| Поле | Требование |
|---|---|
| `Package` | Строгое имя `[a-z0-9][a-z0-9+.-]*`, без пробелов и slash. |
| `Version` | Непустая версия проекта, например `1.0.0`. |
| `Architecture` | `atmkoala-x86_64` или `all`. Linux-specific ABI сюда не подставляется. |
| `Depends` | Необязательный список имён и минимальных версий нативных пакетов. |
| `Entry` | Относительный путь к executable payload под `data/`. |
| `Description` | Короткое описание, одна логическая запись. |
| `License` | Рекомендуется указывать лицензию. |

`Entry` должен указывать на ATMKoala native executable, собранный freestanding toolchain проекта. Обычный Linux ELF с зависимостью от glibc не станет запускаемым приложением только потому, что его переименовали в `.elf`.

## 3. Что должно быть в ATPK/manifest

Каждая строка manifest описывает один data-файл:

```text
apps/weather/weather.elf 18432 7A1C20D4
share/icons/weather.psf 4096 18D03B11
share/doc/weather/README.md 2120 5B9E8A42
```

Формат текущей реализации: `relative-path size crc32`, разделённые пробелами. Путь должен совпадать с путём внутри `data/`, размер должен быть точным, а CRC32 должен совпадать с фактически прочитанными байтами. Для новых версий можно добавить `CRC32C` или SHA-256 как отдельное versioned поле, но нельзя молча менять смысл старого manifest.

## 4. Подготовка дерева проекта

Рекомендуемый каталог разработчика:

```text
weather-package/
├── src/
│   └── weather.c
├── build/
│   └── weather.elf
├── package/
│   ├── ATPK/
│   │   ├── control
│   │   └── manifest
│   └── data/
│       ├── apps/weather/weather.elf
│       ├── share/icons/weather.psf
│       └── share/doc/weather/README.md
└── SHA256SUMS
```

Сначала приложение нужно собрать под ATMKoala ABI, затем скопировать результат в `package/data/apps/weather/weather.elf`. Документы, bitmap fonts и icons также должны находиться под `data/`. Не следует добавлять в пакет `build/`, `.git/`, private keys, сырые дисковые образы и файлы, которые не нужны конечному пользователю.

Пример сборки нативного приложения из исходника проекта:

```sh
cd /path/to/atmkoala/QewoxOS1
make all
```

Команда создаёт системный kernel, но конкретное приложение должно соответствовать native ABI, который экспортирует ATMKoala. Для нового приложения желательно иметь отдельный ABI test, проверяющий entry point, системные вызовы и отсутствие libc/libm dependency.

## 5. Создание ATPK через встроенную команду

В ATMKoala предусмотрен базовый путь:

```text
pkg create weather /path/to/weather.elf
```

Проверить control metadata можно командой:

```text
pkg info weather.atpk
```

Рекомендуемый полный цикл:

```text
pkg create weather /apps/weather/weather.elf
pkg info weather.atpk
pkg install weather.atpk
pkg list
```

Если используется внешний builder, он обязан повторить те же проверки: нормализация relative paths, лимит количества entries, лимит размера каждого entry, отсутствие duplicate paths, проверка CRC32 и проверка допустимой architecture.

## 6. Проверка до публикации

Перед размещением файла для скачивания выполните локальный preflight:

```sh
file weather.atpk
sha256sum weather.atpk
xz --test weather.atpk 2>/dev/null || true
```

Последняя команда не является полноценной проверкой ATPK, потому что контейнер использует zstd и tar framing, а не обязательно standalone `.xz`. Надёжнее использовать встроенный ATMKoala parser:

```text
pkg info weather.atpk
pkg install weather.atpk
```

Проверка считается успешной, если installer принимает control, все manifest entries проходят CRC32, staging завершается без конфликтов, registry обновляется и приложение можно запустить из Exp. Ошибочный пакет должен быть удалён до commit и не должен менять установленный registry.

Для внешнего download-каталога публикуйте рядом:

```text
weather-1.0.0.atpk
weather-1.0.0.atpk.sha256
weather-1.0.0.atpk.README.md
```

Содержимое checksum-файла:

```text
<64 hex characters>  weather-1.0.0.atpk
```

## 7. Установка пользователем

Пользователь скачивает `.atpk`, сверяет checksum и передаёт пакет установщику:

```text
pkg info /downloads/weather-1.0.0.atpk
pkg install /downloads/weather-1.0.0.atpk
pkg list
```

После установки приложение должно появиться в Exp Start menu, если оно зарегистрировано как launcher entry. Если пакет содержит только библиотеку, icon или font, приложение может не появиться в меню.

## 8. Staging и rollback

Installer не должен распаковывать неизвестные данные непосредственно в final path. Безопасный порядок:

```text
1. открыть архив только для чтения;
2. проверить control и architecture;
3. проверить каждый manifest path, размер и CRC32;
4. создать same-directory staging path;
5. распаковать payload только в staging;
6. повторно проверить фактически записанные bytes;
7. проверить отсутствие final path conflicts;
8. переименовать staged files в final paths;
9. обновить package registry последним шагом.
```

Если preflight или commit завершается ошибкой, installer удаляет созданные staging/final paths текущей операции. Замена файлов уже установленного пакета должна быть отдельной операцией с backup policy; текущий ATPK installer не следует считать полноценным atomic upgrade manager.

## 9. Что запрещено

| Запрещённый элемент | Причина |
|---|---|
| Absolute path | Может записать за пределами package root. |
| `..` в пути | Path traversal. |
| Shell/lua/perl maintainer script | Непредсказуемый код установки в kernel environment. |
| Linux glibc executable | Не соответствует ATMKoala native ABI. |
| Device node и raw disk write | Может повредить носитель. |
| Duplicate manifest path | Неоднозначный результат commit. |
| Неподтверждённый checksum | Риск повреждённой или подменённой загрузки. |

## 10. Версионирование и публикация

Имя файла должно быть детерминированным:

```text
<package>-<version>-<architecture>.atpk
```

Например:

```text
minesweeper-1.0.0-atmkoala-x86_64.atpk
```

Не следует перезаписывать уже опубликованный файл с тем же именем. Для каждого изменения выпускайте новую версию, обновляйте `SHA256SUMS` и сохраняйте changelog. Если появится криптографическая подпись, она должна быть отдельным обязательным policy-полем, а не подменой CRC32: CRC32 обнаруживает случайное повреждение, но не доказывает подлинность источника.

## 11. Минимальный чеклист автора

| Проверка | Ожидаемый результат |
|---|---|
| Native ABI | Приложение запускается без Linux userspace. |
| Control | Есть `Package`, `Version`, `Architecture`, `Entry`, `Description`. |
| Paths | Только relative paths, без `..`, slash-prefix и duplicates. |
| Manifest | Каждый data-файл указан ровно один раз. |
| CRC32 | Все значения рассчитаны после окончательной сборки файла. |
| Install | Staging, preflight и rollback проверены на disposable image. |
| Exp | Launcher entry и icon не ломают Start menu. |
| Download | Рядом опубликованы checksum и README. |
| Release | Версия и архив не изменяются после публикации. |

## References

[1]: https://docs.kernel.org/virt/kvm/api.html "The Definitive KVM API Documentation"
[2]: https://www.debian.org/doc/debian-policy/ch-controlfields.html "Debian Policy: Control files and fields"
[3]: https://man7.org/linux/man-pages/man5/deb.5.html "deb(5) package format"
