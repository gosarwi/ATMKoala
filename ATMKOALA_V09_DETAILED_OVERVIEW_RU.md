# ATMKoala v0.9: подробный технический обзор

**ATMKoala** — экспериментальная freestanding операционная система для x86-64. Она создаётся как самостоятельная программная платформа: kernel, загрузочные пути, графическая среда, файловые системы, shell, native userspace ABI и статическая C-библиотека собираются в одном репозитории. Проект не использует glibc внутри ОС и ориентирован на небольшой контролируемый слой POSIX-подобных интерфейсов, достаточный для постепенного развития собственного userspace.

> **Статус v0.9.** Это активная инженерная ветка, а не финальный release. В ней подтверждены загрузка, ядро, CPL 3, syscall boundary, task-local файловые дескрипторы и native static ELF. Часть графических приложений, файловых драйверов и будущая Toybox-совместимость остаются развиваемыми компонентами.

| Параметр | Текущее состояние |
|---|---|
| Архитектура | x86-64, long mode, Multiboot2-compatible kernel path |
| Язык и стиль | GNU99 C + x86-64 ASM, freestanding build, без hosted libc |
| Kernel base / heap | 64 MiB / 128 MiB |
| Userspace window | `0x40000000`, размер 2 MiB |
| Основная графическая среда | **Exp** — минималистичный desktop с нижней панелью |
| UI scale | Зафиксирован на **100%** |
| Native ABI | `int $0x80`, negative errno-style return values |
| Основной формат приложений | Static ELF64 `ET_EXEC` |
| Boot paths | GRUB ISO, BIOS ATMBOOT, UEFI ATMUEFI |

## 1. Идея и границы проекта

ATMKoala не пытается быть готовой заменой Linux, Windows или BSD. Это ОС, в которой каждый базовый слой можно проследить от загрузчика до user application: от включения long mode и page tables до выполнения собственного ELF процесса на privilege level 3. Такой подход удобен для обучения, исследований архитектуры ОС и постепенного выращивания полностью native software stack.

Основная работа v0.9 концентрируется на **kernel/userspace ветке**. BIOS ATMBOOT и UEFI ATMUEFI имеют рабочие проверенные меню и временно считаются замороженными: дальнейшая работа над POSIX, libc, Toybox и Exp не должна изменять их каталоги, layout образов или handoff contract.

| Цель | Практическая интерпретация |
|---|---|
| Самостоятельность | Система не рассчитывает на Linux kernel ABI или glibc внутри guest OS. |
| Контролируемый POSIX subset | Сначала реализуются маленькие, проверяемые операции `read`, `write`, `open`, memory growth и process lifecycle. |
| Native userspace | Приложение собирается в статический ELF для ATMKoala и запускается из kernel loader. |
| Безопасная эволюция | Внешние границы user/kernel и копирование буферов проверяются до добавления больших app frameworks. |
| Компактный GUI | Exp ориентирован на минимализм, а не на imitation full desktop OS. |

## 2. Загрузка и запуск kernel

В проекте есть три способа загрузить один и тот же kernel image. GRUB ISO служит универсальным recovery/development path. ATMBOOT предназначен для BIOS-совместимой raw IDE boot chain, а ATMUEFI — для загрузки как UEFI application с ESP. Все три направления были проверены в QEMU/OVMF после relocation kernel в безопасную область физической памяти [1].

### 2.1. GRUB ISO

GRUB ISO остаётся наиболее удобным способом запуска на development машине. Kernel принимает Multiboot2 handoff, после чего настраивает память, interrupts, VGA/VBE или compatibility text mode и стартует user-facing shell/Exp.

### 2.2. ATMBOOT: BIOS путь

ATMBOOT состоит из минимального stage 0 и stage 2. Stage 2 использует BIOS EDD, ATA PIO LBA28 и VBE, загружает **flat** kernel payload и передаёт kernel framebuffer information. Использование `objcopy -O binary` критично: raw loader не может положить ELF container как обычный flat image, потому что file headers смещают machine code относительно VMA [1].

### 2.3. ATMUEFI: UEFI путь

ATMUEFI создаёт GPT image с EFI System Partition. ESP содержит `\\EFI\\BOOT\\BOOTX64.EFI` и `\\EFI\\ATMKOALA\\KERNEL.BIN`. UEFI loader использует GOP и Simple File System, корректно выходит из Boot Services и передаёт управление 64-bit kernel entry. Для совместимости с OVMF kernel reloads its own GDT code selector before normal interrupt operation [1].

### 2.4. Unified loader menu

В BIOS и UEFI реализовано одинаковое 2×2 menu contract:

| Пункт | Назначение |
|---|---|
| **Exp Desktop** | Обычный графический запуск Exp. |
| **Compat Console** | Текстовый-safe маршрут с kernel `novbe` handoff. |
| **Disk Installer** | Выделенный графический installer dispatch. |
| **Restart** | Перезапуск/возврат по boot path contract. |

Все tile paths ранее проверялись в QEMU или OVMF. В частности, tile 2 не инициализирует VBE/GOP handoff как декоративную опцию, а действительно передаёт `novbe` kernel command line [1].

## 3. Ядро, память и планировщик

Kernel написан на C и ASM, использует собственные memory, paging, interrupt и scheduler компоненты. Физическая база kernel сейчас равна **64 MiB**, kernel heap начинается с **128 MiB**. Это relocation устраняет конфликт с типичными низкими UEFI allocations и был проверен одновременно для ISO, BIOS и UEFI путей [1].

> **Принцип memory layout:** kernel memory и userspace не смешиваются. Каждый native process получает отдельное представление в ограниченном user virtual window, а user pointer проверяется перед тем, как kernel использует его при syscall.

### 3.1. Paging и NX

x86-64 paging используется не только для запуска long mode, но и как граница native userspace. Для защиты non-executable memory включён `EFER.NXE` в BIOS long-mode и UEFI entry paths. Это не делает ОС завершённо защищённой multiuser system, но устраняет класс опасных случаев, когда writable data page неявно executable.

### 3.2. Scheduler и завершение задач

Native process lifecycle включает static ELF load, переход на CPL 3, `_exit(status)` и `waitpid` в parent/kernel path. В scheduler исправлены удаление из очереди в bounded form и переход `task_exit_from_syscall()` напрямую к scheduler handoff — без вложенного IRQ контекста. Context switch использует post-call `RSP+8` contract, важный для безопасного выхода из user syscall path [2].

| Возможность | Подтверждённое состояние |
|---|---|
| Static ELF64 `ET_EXEC` launch | Реализовано. |
| CPL 3 execution | Реализовано и проверено. |
| Exit status | `exit(42)` передаётся в process lifecycle. |
| `waitpid` | Проверен вместе с native process exit. |
| User virtual range | Ограниченное 2 MiB process window. |
| `brk` heap | Bounded process heap. |

## 4. Native POSIX-подобный ABI

ATMKoala v0.9 использует простой x86-64 ABI через `int $0x80`: номер syscall хранится в `RAX`, первые аргументы передаются в `RDI`, `RSI`, `RDX`, а ошибка возвращается как negative errno-style result. Public declarations находятся в [`sdk/atm_native_abi.h`](sdk/atm_native_abi.h).

| Syscall | Номер | Назначение |
|---|---:|---|
| `read` / `write` | 0 / 1 | Потоковый ввод и вывод. |
| `open` / `close` | 2 / 3 | Открытие и закрытие native file handle. |
| `fstat` / `lseek` | 5 / 8 | Метаданные файла и позиционирование. |
| `brk` | 12 | Bounded userspace heap. |
| `getpid` / `getppid` | 39 / 110 | Идентификация процесса. |
| `exit` / `waitpid` | 60 / 61 | Завершение и ожидание процесса. |
| `kill` | 62 | Управление task lifecycle. |
| `getuid` / `getgid` / `gettid` | 102 / 104 / 186 | Базовые identity/thread identifiers. |
| `ABI_INFO` | `0xA700` | Version/capability information. |

### 4.1. User-copy boundary

Kernel не доверяет user pointers. Для `read`, `write`, `open` и `fstat` user memory проверяется и копируется через отдельную user-access boundary. Это защищает kernel от некорректного диапазона адресов и не позволяет syscall handler безусловно dereference pointer из CPL 3 [2].

### 4.2. File descriptors

У каждого task есть собственное namespace из 32 fd handles. На старте fd `0`, `1` и `2` связаны с `/dev/tty`. Handles закрываются при exit. Такое разделение важно: userspace executable не должен случайно использовать kernel-global descriptor state [3].

## 5. Static native libc

В каталоге `sdk/libc/` создана собственная компактная C-библиотека. Она собирается с `-nostdlib -nostdinc`, то есть не использует glibc и не зависит от hosted runtime. CRT вызывает `main(argc, argv, envp)`, затем передаёт код возврата в `atm_exit`.

| Компонент | Содержимое |
|---|---|
| `crt0.s`, `crt.c` | Вход приложения, передача управления в `main`, exit path. |
| `string.c` | `mem*` и `str*` primitives. |
| `errno.c` | Single-threaded errno storage и syscall result normalization. |
| `unistd.c` | `open`, `close`, `read`, `write`, PID wrappers, `_exit`. |
| `malloc.c` | Bounded brk-backed `malloc`, `calloc`, `realloc`, `free`. |
| `stdlib.c` | `exit`, `abs`, `strtol`. |
| `atm_gui_stub.c` | GUI ABI v1 capability stubs с `ENOSYS`. |

Smoke executable проверяет allocation, string operations, `realloc`, GUI capability negotiation и write path. Эта проверка является важнее, чем номинальный compile: она подтверждает, что пользовательный ELF получает working CRT, heap и syscall access в CPL 3 [2].

## 6. Графическая среда Exp

**Exp** — основная desktop environment ATMKoala. Её дизайн направлен в сторону тёмного, чёрного минимализма с нижней панелью, окнами, terminal experience и набором native applications. Exp не является X11/Wayland server и не выдаётся за него: это собственный GUI layer поверх framebuffer/VBE graphics.

Стабильность на real hardware была отдельной задачей v0.9. UI scale теперь фиксирован на **100%**; `EXP_SCALE(v)` и `EXP_UNSCALE(v)` являются identity macros. Удалены scale buttons, persistence и runtime variable scale paths, потому что они создавали нестабильные graphical states.

| Исправление | Причина и результат |
|---|---|
| Удалён `pit_sleep(300)` перед стартом Exp | Убран зависающий/лишний delay path. |
| `hwinfo` использует cached CPU data | Исключены повторная PIT calibration и risky MSR read. |
| EC telemetry по raw ports выключен | `ATM_EC_TELEMETRY_ENABLED=0` исключает опасный ACPI EC polling. |
| Scale закреплён на 100% | Исключены нестабильные scaling/reflow paths. |
| Tray telemetry ограничена | Снижена вероятность platform-specific hardware hangs. |

Проверка производилась для проблемных сценариев `de` и `hwinfo`; состояние и инструкция smoke test задокументированы в `V09_REAL_HARDWARE_STABILITY_AUDIT.md` и `REAL_HARDWARE_SMOKE_V09_RU.md` [4].

## 7. GUI ABI для сторонних приложений

Публичный [`sdk/atm_gui.h`](sdk/atm_gui.h) описывает версионированный **Native GUI ABI v1**: handles, rectangles, window/surface/event layouts и capability bits. На текущем этапе `runtime_info()` работает и сообщает capability state, а остальные GUI calls являются честными stubs с `ENOSYS`.

Это сознательное решение. ABI contract можно стабилизировать и проверять до реализации compositor/window manager surface API. Приложение получает возможность проверить версию и capabilities, а не полагаться на несуществующую GUI функцию.

## 8. Хранение данных, диски и файловые системы

В kernel source присутствуют ATA/disk, partition management и filesystem components: FAT32, ext2, Btrfs и CatFS/VFS integration. System Monitor выводит real detected ATA model/capacity/LBA mode, I/O history и read/write/error counters; это было проверено на QEMU IDE image [1].

Однако наличие source component не означает, что все операции каждой файловой системы одинаково завершены или готовы для production data. В текущем обзоре корректнее разделять уровень интеграции:

| Подсистема | Что можно считать реализованным | Осторожное ограничение |
|---|---|---|
| ATA/disk discovery | Детект model, capacity, LBA mode и I/O counters. | Реальная hardware matrix ещё ограничена. |
| VFS/native files | Используются в POSIX `open/read` flows. | Семантика ещё расширяется. |
| FAT32/ext2/Btrfs source | Компоненты присутствуют в kernel build. | Полнота read/write и recovery semantics должна проверяться отдельно. |
| Partition manager/installer | Компоненты и dedicated installer route существуют. | Не следует использовать для ценных данных без отдельного media QA. |

## 9. Встроенные приложения и shell

ATMKoala содержит собственную shell/terminal среду, Exp launcher, System Monitor, disk/installer interfaces, GUI demos, file/image format components и простые games/application modules. Maze удалён из kernel, launcher и store catalog, потому что прежняя команда могла приводить к panic behaviour [1].

Графические и application modules следует рассматривать как часть platform prototype. Их назначение — создавать native UI и test surface для kernel services, а не обещать полный набор desktop software.

## 10. Toybox-ориентированный userspace

Следующее направление v0.9 — **Toybox-compatible native command runtime**. Upstream Toybox выбран вместо BusyBox прежде всего из-за permissive license: его license допускает use, copy, modification и distribution с отказом от гарантий [5]. При этом upstream Toybox — hosted Linux/Android-oriented program: normal dispatcher опирается на generated configuration, full libc, stdio, locale и platform facilities [6].

Поэтому готовый upstream Linux binary не может быть запущен как ATMKoala application. В репозиторий добавлен upstream snapshot `third_party/toybox/`, pinned to **0.8.14** commit `b7ec52ac35e075caffca5d330995d44e8dbfc8c3`, а также attribution record `ATMKOALA_VENDOR.md`.

Первый собственный freestanding compatibility subset находится в `sdk/toybox_atm/toybox_atm.c` и уже компилируется against existing native libc headers. Он реализует Toybox-style dispatch и самый маленький набор `true`, `false`, `echo`, `cat`, `toybox --list`. Это начало адаптации, **не заявка на full upstream Toybox port**.

| Состояние Toybox direction | Значение |
|---|---|
| Upstream source acquired | Да, vendor copy и provenance зафиксированы. |
| Minimal native dispatcher compiles | Да, использует только ATM `open/read/write/close`. |
| Embedded `toybox.elf` fixture | Следующий integration step. |
| `printf`/`wc` | Требуют narrow formatter и public `fstat` libc wrapper. |
| Typed `toybox <applet>` from shell | Требует native spawn/exec argv stack; текущий loader gives `argc=0`. |
| Full Toybox/Linux compatibility | Не реализована и не является ближайшей целью. |

## 11. Проверки и фактические гарантии

Команда `posix test` является центральной regression point. На ISO, ATMBOOT tile 1 и ATMUEFI tile 1 были подтверждены результаты:

```text
paging=OK
uaccess=OK
vfs-posix=OK
syscall-usercopy=OK
process-fd=OK
native-cpl3=OK
static-libc=OK
```

Это подтверждает именно базовую chain: memory protection assumptions, user-copy, VFS-facing syscall behaviour, per-process FDs, static ELF transition to CPL 3 and native libc smoke application. Это **не** означает автоматическую совместимость с Linux binaries, POSIX certification или multiuser security review [1] [2].

## 12. Сборка и запуск

Рабочая директория проекта:

```bash
cd /home/ubuntu/atmkoala_fixed/QewoxOS1
```

| Команда | Назначение |
|---|---|
| `make all` | Собрать kernel и встроенные native fixtures. |
| `make iso` | Создать GRUB ISO `atmkoala-OS-v0.5.iso`. |
| `make run` | Запустить ISO в QEMU VGA configuration. |
| `make run-vbe` | Запустить ISO в QEMU с standard VBE VGA. |
| `make atmboot` | Собрать raw BIOS/IDE image. Не требуется для userspace work. |
| `make atmuefi` | Собрать UEFI GPT ESP image. Не требуется для userspace work. |

Для текущей main branch рекомендуется начинать с `make all`, `make iso`, затем QEMU regression и `posix test`. Изменения в Toybox/libc не должны автоматически trigger changes in frozen bootloader directories.

## 13. Честные ограничения v0.9

ATMKoala сейчас не имеет complete Unix process model. Нет подтверждённого `execve` with argv/envp stack, shell pipes, job control, signals in full POSIX sense, sockets, dynamic linker, shared libraries, locale runtime, standard stdio или full directory iteration. GUI ABI пока capability stub, а filesystem write coverage и recovery behaviour требуют отдельной validation.

Система также не должна использоваться как daily-driver или для хранения единственных копий важных данных. Она предназначена для development, QEMU/controlled hardware testing и постепенного расширения platform contract.

## 14. Ближайший roadmap

| Приоритет | Работа | Результат |
|---:|---|---|
| 1 | Native spawn/exec argv stack | Реальное `toybox <applet> [args…]` из shell. |
| 2 | libc output and `fstat` layer | `printf`, `wc`, diagnostics and broader file tools. |
| 3 | Embed `toybox.elf` and test in CPL 3 | Regression fixture alongside `libc_smoke.elf`. |
| 4 | Expand VFS/process APIs | Directory listing, richer filesystem tools, safe process utilities. |
| 5 | GUI ABI capabilities | Реальные windows/surfaces/events after stable kernel contract. |
| 6 | Separate filesystem and hardware QA | Проверяемая matrix read/write/recovery before user-data claims. |

## Заключение

ATMKoala v0.9 уже прошла важный переход от monolithic kernel demo к системе с реальным **native userspace boundary**. Она умеет загружаться несколькими firmware paths, изолировать static ELF process в userspace window, безопасно принимать буферы через syscall boundary, управлять task-local FDs, выделять bounded heap и возвращать из CPL 3 в scheduler. Exp обеспечивает собственный GUI layer, при этом risky real-hardware paths были intentionally simplified for stability.

Следующая логичная цель — не переносить Linux целиком, а расширять маленький, проверяемый ATM-native contract. Toybox-ориентированный multicall runtime, собственная libc и versioned GUI ABI дают для этого более правильную основу, чем попытка сразу запускать большой Linux userspace.

## References

[1]: [QA_ATMBOOT_NOTES.md](QA_ATMBOOT_NOTES.md) — проверенные boot, VBE/GOP, monitor и v0.9 regression results.

[2]: [src/native_app.c](src/native_app.c), [src/atm_syscall.c](src/atm_syscall.c), [src/sched.c](src/sched.c) — native ELF launch, syscall boundary и task exit mechanics.

[3]: [src/native_fd.c](src/native_fd.c), [sdk/atm_native_abi.h](sdk/atm_native_abi.h) — native FD namespace и public syscall ABI.

[4]: [V09_REAL_HARDWARE_STABILITY_AUDIT.md](V09_REAL_HARDWARE_STABILITY_AUDIT.md), [REAL_HARDWARE_SMOKE_V09_RU.md](REAL_HARDWARE_SMOKE_V09_RU.md) — changes for real-hardware stability and smoke testing.

[5]: [Toybox LICENSE](third_party/toybox/LICENSE) — upstream permissive license text.

[6]: [Toybox upstream overview](https://landley.net/toybox/) и [Toybox FAQ](https://landley.net/toybox/faq.html) — multicall model and hosted project architecture.
