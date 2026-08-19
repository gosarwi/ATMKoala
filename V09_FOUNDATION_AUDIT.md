# ATMKoala v0.9 — аудит фундаментального POSIX и ABI слоя

**Статус:** исходная точка для работ v0.9.  
**Область:** POSIX runtime, системные вызовы, процессы, память, ELF и прикладной ABI.

## Вывод

ATMKoala v0.5 уже содержит значимый фундамент для собственной графической среды: VFS, файловые дескрипторы, task-local `cwd` и `umask`, идентификаторы задач, базовый ELF64 user-image loader, переход в CPL 3 и native AWM. Однако эти компоненты пока образуют **kernel-side portable subset**, а не завершённый userspace. Поэтому первоначальная цель v0.9 должна состоять в создании безопасного, документированного ABI для нативных статически собранных приложений. Прямая совместимость с Linux binary ABI и запуск LXQt не являются достижимым первым шагом.

| Подсистема | Уже имеется | Граница текущего решения |
|---|---|---|
| Файлы | `open`, `close`, `read`, `write`, `pread`, `pwrite`, `readv`, `writev`, `lseek`, `stat`, каталоги и метаданные через `atm_posix_*`. | Все обёртки работают с kernel pointers и VFS напрямую; отсутствует ABI boundary для CPL 3. |
| Контекст задачи | PID/PPID, UID/GID, `cwd`, `umask`, наследование атрибутов, zombie/reap. | Задачи создаются как kernel functions на kernel CR3; нет user process object. |
| Системные вызовы | Номера, похожие на x86-64 Linux (`read`, `write`, `close`, `lseek`, `exit`, `waitpid`, `kill`, `getpid` и т. д.) и регистровый контракт. | Gate ещё не является полноценным CPL 3 ABI; `open` объявлен, но не dispatch-ится; user-copy и errno semantics неполны. |
| Память пользователя | Есть page table для user space, границы `ATM_USER_BASE..ATM_USER_TOP`, user stack и NX/write mapping. | Нет `brk`, `mmap`, `munmap`, demand paging, shared memory и безопасной lifetime-модели address space. |
| ELF | `elf64_load_user()` валидирует и загружает native ELF64 `PT_LOAD` сегменты в user window. | Нет `execve`, program interpreter, relocation/dynamic loader, TLS, argv/envp или привязки образа к scheduler task. |
| GUI | AWM уже предоставляет native surfaces, очередь событий и focus; Exp использует VBE/TTF/AWM. | Нет userspace client ABI, compositor protocol, буферов клиента, permission model или toolchain для сторонних GUI-приложений. |

## Значение для собственного GUI

Собственный GUI может стать главным потребителем ABI v0.9. Для этого не требуется копировать X11, Wayland, Qt или Linux syscall ABI побайтно. Достаточно определить стабильный ABI ATM Application Runtime: статический ELF64, фиксированная версия system call table, opaque file/window handles, событийная очередь, memory-safe user-copy границы и минимальные операции процесса. AWM должен стать server-side реализацией этого ABI, а не набором вызовов, доступных только коду ядра.

Такой путь позволяет сначала создать реально исполнимые GUI-приложения: terminal, file manager, settings и image viewer будут запускаться как native user processes с отдельными адресными пространствами. Это также снижает риск: ошибочное приложение не сможет напрямую передать kernel pointer или повредить compositor.

## Почему LXQt не является стартовой целью

LXQt предназначен для зрелой Linux userspace среды. До его реального запуска потребуется существенно больше, чем графический сервер: динамический linker ELF, полноценная libc, pthreads и futex, Unix sockets, `poll`/`epoll`, процессы `fork`/`execve`, `mmap`, IPC, D-Bus, сетевые API, DRM/GBM/EGL и Qt runtime. Наличие отдельных Linux-подобных номеров системных вызовов само по себе не даёт Linux binary compatibility.

Следовательно, v0.9 будет ориентирован на **Linux-like source and ABI conventions**, но не заявляет совместимость с Linux binaries. Это оставляет в будущем два открытых направления: перенос отдельных POSIX-программ через compatibility runtime и постепенное расширение ABI до более широкого Linux-профиля.

## Приоритеты реализации

| Приоритет | Изменение | Результат |
|---|---|---|
| P0 | Безопасный `copy_from_user`/`copy_to_user`, CPL 3 syscall gate, error contract. | Системные вызовы не работают с user pointers как с kernel pointers. |
| P0 | User-process object, scheduler binding, `execve`-подобный запуск статического ELF64. | Изолированное нативное приложение можно загрузить и запустить. |
| P0 | Таблица дескрипторов процесса и stdio. | File I/O принадлежит процессу, а не глобальному kernel context. |
| P1 | `brk`/анонимный `mmap`/`munmap`, argv/envp, `getcwd` и базовые environment calls. | C runtime и GUI toolkit получают рабочую память и параметры запуска. |
| P1 | ABI manifest и стабильный syscall/API versioning. | Стороннее приложение может проверить совместимость до запуска. |
| P2 | `poll`/event handles, pipe, signal delivery baseline, shared compositor buffers. | Event-driven GUI перестаёт зависеть от busy loop. |
| P2 | Dynamic linker roadmap, shared libraries, TLS и threads. | Основа для крупных портов после v0.9. |

## Целевой baseline v0.9

Первый практический milestone v0.9 — это **ATM Native App ABI v1**: статически слинкованная ELF64 программа запускается в CPL 3 с отдельным address space, получает `argc`/`argv`/`envp`, использует документированные syscall wrappers для stdin/stdout/stderr, файлов, памяти, времени, событий и завершения, а GUI-клиент взаимодействует с AWM через versioned native protocol. Именно после достижения этого baseline следует начинать переносить Exp-компоненты из kernel context в приложения.

## Нормативная опора

POSIX определяет интерфейсы системных вызовов, заголовки и базовую среду portable applications; его следует использовать как source-level профиль для native runtime, не как обещание Linux binary compatibility.[1] Linux man-pages подчёркивают, что system call — boundary между приложением и ядром, а wrapper runtime отвечает за нормализацию отрицательных ошибок в `errno` и за сохранение ABI-совместимости.[2] Для ATMKoala это подтверждает необходимость держать native syscall ABI узким, versioned и стабильным, а POSIX-совместимость предоставлять отдельным C-facing runtime layer.

## References

[1] [The Open Group Base Specifications Issue 7, POSIX System Interfaces](https://pubs.opengroup.org/onlinepubs/9699919799.2018edition/)

[2] [Linux man-pages: syscalls(2)](https://man7.org/linux/man-pages/man2/syscalls.2.html)

ELF GABI определяет для `PT_LOAD` копирование `p_filesz` bytes в memory segment и нулевую инициализацию диапазона `p_memsz - p_filesz`; это соответствует уже начатому native loader и делает static ELF64 разумным первым execution profile.[3] AMD64 System V ABI остаётся ориентиром для соглашения о вызовах и формы initial process context, однако ATMKoala v0.9 примет только консервативное подмножество, нужное для статически слинкованных native programs.[4]

[3] [ELF gABI: Program Loading](https://gabi.xinuos.com/elf/07-pheader.html)

[4] [System V Application Binary Interface: AMD64 Architecture Processor Supplement](https://refspecs.linuxbase.org/elf/x86_64-abi-0.98.pdf)

## Реализованный инкремент: syscall user-copy boundary

Первый кодовый инкремент v0.9 добавил к dispatcher CPL 3-safe пути для `open`, `read`, `write`, `fstat` и output pointer `waitpid`. Для user buffers применяются `copy_from_user` и `copy_to_user`; pathname копируется в kernel-owned bounded buffer, а недопустимый диапазон возвращает `-ATM_EFAULT`. Одно I/O обращение v1 ограничено 64 KiB до появления process-owned asynchronous I/O.

Команда `posix test` в свежей QEMU загрузке успешно вывела `paging=OK uaccess=OK vfs-posix=OK syscall-usercopy=OK`. Новый `syscall-usercopy` self-test создаёт и привязывает временное user address space, вызывает dispatcher как CPL 3, проверяет `open → write → close → open → read → fstat` и убеждается, что выход за `ATM_USER_TOP` отклоняется с `EFAULT`.

## Финальная регрессия текущего инкремента

После изоляции experimental launcher fresh QEMU ISO regression вернула terminal prompt без panic. Экран подтвердил: `paging=OK`, `uaccess=OK`, `vfs-posix=OK`, `syscall-usercopy=OK`. Это является текущим безопасным baseline: Linux-like source-oriented POSIX foundation и проверяемая boundary для CPL 3 pointers есть; полноценный user-process execution lifecycle намеренно не объявлен готовым до устранения documented `iretq` context-switch blocker.

## CPL 3 lifecycle: исправлено и проверено

Static ELF64 execution slice теперь проходит реальную QEMU regression. Причиной первого GP #13 был nested `task_yield()` из `exit` внутри активного `int 0x80` frame в сочетании с сохранением pre-return RSP в `context_switch`, который затем resume-ится через `jmp`. Введён direct `task_exit_from_syscall()` handoff и post-call `RSP+8` contract для context switch; bounded run-queue removal защищает от бесконечной traversal. Fresh QEMU screen показал `native-cpl3=OK` после generated CPL 3 probe с `exit(42)` и parent `waitpid` reaping.

## Процессные дескрипторы: проверено

Native tasks теперь получают собственное пространство из 32 descriptor handles: `0`, `1`, `2` наследуют console backend, а новые handles отображаются в backend VFS descriptor только внутри task-local `fd_map`. User-mode `open`, `close`, `read`, `write`, `lseek` и `fstat` маршрутизируются через это отображение; завершение native task закрывает его private handles. Fresh QEMU regression показала единый успешный результат: `paging=OK uaccess=OK vfs-posix=OK syscall-usercopy=OK process-fd=OK native-cpl3=OK`.

## Память процесса и C-facing API

В native task добавлены `user_brk_base` и `user_brk`. `ATM_SYS_BRK=12` возвращает текущий break при `brk(0)`, растит private anonymous heap page-aligned внутри user window и возвращает current break при недопустимом request; shrink пока логический, без physical unmap. Generated ELF probe расширен проверкой `brk(0)` и `brk(old+32)` до `exit(42)`, поэтому QEMU `native-cpl3=OK` подтверждает этот путь.

Public header `sdk/atm_native_abi.h` задаёт ATM Native App ABI v1, raw `int $0x80` wrappers и минимальные C-facing calls для файлов, процесса и `brk`. Отдельный freestanding compile probe успешно собран с `-nostdlib`, подтверждая, что third-party static application может включить header без kernel-private dependencies.

## ABI stability и dynamic-linking roadmap

`V09_DYNAMIC_LINKING_ROADMAP.md` закрепляет static `ELF64 ET_EXEC` как deliberate v0.9 application profile. Он отделяет completed `PT_LOAD` execution slice от future `ET_DYN`/`PT_INTERP` work и задаёт dependency order: process memory first, then relocations, then native libc/TLS, then graphics portability. Это предотвращает неверное обещание немедленного запуска Linux Qt/LXQt or Mesa binaries и оставляет v0.9 свободным для собственного static AWM GUI ABI.

## OVMF / ATMUEFI regression

Rebuilt `atmkoala-atmuefi.img` был загружен как USB ESP в fresh OVMF. Unified ATM Loader принял tile `1`, подтвердил `GOP 1280x800 ready`, после чего ядро подтвердило VBE handoff. В Exp terminal команда `posix test` завершилась prompt return и показала: `paging=OK uaccess=OK vfs-posix=OK syscall-usercopy=OK process-fd=OK native-cpl3=OK`.

## BIOS / ATMBOOT regression

Rebuilt `atmkoala-atmboot.img` был загружен as IDE raw disk in fresh QEMU. Unified BIOS ATM Loader tile `1` передал VBE graphics (`1024×768×24`, `vbe OK`) в Exp. В terminal `posix test` вернул prompt и те же passing results: `paging=OK uaccess=OK vfs-posix=OK syscall-usercopy=OK process-fd=OK native-cpl3=OK`. Таким образом v0.9 execution baseline проверен на GRUB ISO, BIOS ATMBOOT и UEFI ATMUEFI paths.
