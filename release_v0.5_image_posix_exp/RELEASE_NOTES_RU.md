# ATMKoala v0.5 — Exp, Image Viewer и POSIX portable-userland

**Сборка:** `ATMKoala-v0.5-Exp-ImageViewer-POSIX.iso`  
**Архитектура:** freestanding x86-64, C + ASM, Multiboot2, без libc  
**Статус:** собрана и проверена в QEMU с VBE 800×600.

> В этом обновлении Image Viewer является нативным приложением Exp, а не внешним инструментом. PNG и JPEG декодируются внутри ядра через ограниченную freestanding-интеграцию `stb_image`.

## Что добавлено

| Область | Результат |
|---|---|
| **PNG/JPEG** | Поддерживается определение форматов по сигнатуре и расширению: `.png`, `.jpg`, `.jpeg`. |
| **Image Viewer** | Новое приложение Exp с запуском из launcher, иконки рабочего стола и маршрутизацией из Files. |
| **Безопасные пределы decoder** | Размер файла ограничен 8 MiB, размер стороны — 4096 px, площадь — 2 млн пикселей. SIMD, stdio, HDR, linear conversion и TLS отключены для freestanding-окружения. |
| **Отрисовка** | Пропорциональное размещение в viewport, nearest-neighbour scaling, альфа-композиция поверх checkerboard. |
| **Масштаб Image Viewer** | `+` увеличивает относительно fit-режима с шагом 25%; `-` уменьшает; `0` или `F` возвращают fit. Текущее значение показывается в footer. |
| **Файловый менеджер** | Двойное открытие PNG/JPEG/BMP в Files направляет файл в Image Viewer, остальные файлы — в обычный Viewer. |
| **Exp chrome** | Добавлены двухслойные нейтральные тени, фокусный тёплый divider и единообразные framed-кнопки окна. |
| **About** | Переработана информационная карточка Exp; текст уложен в default UI scale 130% и использует только ASCII-совместимые разделители. |
| **POSIX** | Рабочий набор расширен task-local cwd/umask, нормализацией путей, `creat`, `pread`, `pwrite`, `readv`, `writev`, `fsync`, `fdatasync`, `getpid`, `getppid`. |
| **BusyBox contract** | Контракт повышен до ABI **1.1** и фиксирует наличие `IOV`, `FSYNC` и task-local cwd. |

## Использование Image Viewer

При каждой чистой загрузке в `/home` создаются два демонстрационных файла, если пользователь не создал одноимённые файлы ранее:

| Файл | Назначение |
|---|---|
| `/home/exp-sample.png` | Проверка PNG, прозрачности и checkerboard-фона. |
| `/home/exp-sample.jpg` | Проверка baseline JPEG decoder. |

Откройте **Files**, перейдите в `/home` и откройте нужный файл. В Image Viewer нижняя строка показывает формат, исходные размеры и режим масштаба. Клавиши `+`, `-`, `0` и `F` работают при активном окне приложения.

| Действие | Результат |
|---|---|
| `+` | 125% от fit после первого нажатия, затем шаг 25% до 400%. |
| `-` | Понижает относительный масштаб; из fit переходит на 75%. |
| `0` / `F` | Пропорционально вписывает изображение в рабочую область. |
| Закрытие окна | Освобождает буфер RGBA decoder. |

## Exp и визуальная политика

Exp сохраняет установленную ранее политику: **Dark Mono** и **White Paper** переключаются только из **Settings → Appearance**; shell-команды `theme` и `themes` не возвращались. Доступны масштабы 80%, 90%, 100%, 130% и 150%, с сохранением выбранного значения. Палитра VGA Caramel независима от темы Exp и не изменяется переключателем GUI.

Обновлённый chrome не использует неоновые или синие градиенты. Его задача — сделать активное окно читаемым за счёт небольшой тени, тонкого фокусного divider и согласованных кнопок minimize/maximize/close.

## POSIX portable-userland

Уровень POSIX теперь лучше пригоден для статически адаптируемого native userland. Текущий контекст cwd и umask хранится в `task_t`, а новая задача наследует его от создателя. Нормализатор путей обрабатывает повторные `/`, `.` и `..` до вызова VFS. VFS продолжает самостоятельно разрешать symbolic links.

| API | Семантика в этой версии |
|---|---|
| `atm_posix_creat` | Создаёт/обнуляет файл в write-only режиме с применением task-local umask. |
| `atm_posix_pread`, `atm_posix_pwrite` | Выполняют positioned I/O и восстанавливают текущую позицию descriptor после операции. |
| `atm_posix_readv`, `atm_posix_writev` | Последовательно обрабатывают до 16 iovec-элементов. |
| `atm_posix_fsync`, `atm_posix_fdatasync` | Проверяют descriptor и выполняют `catfs_sync()` при активном CatFS mount; для in-memory VFS являются успешной no-op операцией. |
| `atm_posix_getpid`, `atm_posix_getppid` | Возвращают PID/PPID текущего scheduler task. |
| cwd и umask | Изолированы на уровне kernel task; обычный shell использует idle task context. |

> **Честная граница реализации:** это расширенный portable subset, а не заявление о полной POSIX-сертификации. `fork`, полноценный `execve` с argv/env stack, изолированные FD tables, pipes/FIFO, complete signal semantics, sockets, `fcntl` и Linux ioctl пока не рекламируются как готовые. ELF64 loader и scheduler foundation существуют, но их связывание в полноценный process/exec path остаётся следующим этапом.

## Проверка в QEMU

Собранный ISO проверялся в QEMU с VBE framebuffer 800×600 и 256 MiB памяти. Были пройдены следующие сценарии:

| Сценарий | Статус | Артефакт |
|---|---|---|
| VBE boot в Exp | Пройден | `screenshots/exp-about-130.png` |
| Files → `/home/exp-sample.jpg` → Image Viewer | Пройден | `screenshots/jpeg-fit.png` |
| JPEG relative zoom 125% | Пройден | `screenshots/jpeg-zoom-125.png` |
| Files → `/home/exp-sample.png` → Image Viewer | Пройден | `screenshots/png-fit.png` |
| `posix test` | Пройден: `paging=OK uaccess=OK vfs-posix=OK` | `screenshots/posix-selftest.png` |

Первый прогон JPEG выявил page fault внутри `stb_image`: compiler-generated TLS использовал `%fs`, недопустимый в текущем kernel runtime. Интеграция была исправлена через `STBI_NO_THREAD_LOCALS`, после чего JPEG и PNG были повторно проверены в чистой QEMU загрузке.

## Состав release

| Файл или каталог | Содержание |
|---|---|
| `ATMKoala-v0.5-Exp-ImageViewer-POSIX.iso` | Готовый загрузочный ISO. |
| `ATMKoala-v0.5-Exp-ImageViewer-POSIX-src.tar.gz` | Исходный архив именно этой сборки, без build/ и ранее собранных ISO. |
| `QA_IMAGE_VIEWER_NOTES.md` | Журнал QA, включая первоначальную ошибку decoder и её исправление. |
| `screenshots/` | Captures JPEG, PNG, Exp About и POSIX self-test. |

## Быстрый запуск

```bash
qemu-system-x86_64 \
  -cdrom ATMKoala-v0.5-Exp-ImageViewer-POSIX.iso \
  -m 256M -vga std -no-reboot
```

GRUB entry по умолчанию запускает графический Exp. Из текстового shell команда `de` возвращает в Exp. Для installer сохранён отдельный GRUB entry; normal desktop не запускает destructive disk installer.

