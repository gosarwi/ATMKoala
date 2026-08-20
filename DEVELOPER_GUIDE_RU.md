# Руководство разработчика ATMKoala v0.9

**Статус:** практическая документация для development-линии `v0.9.0-dev.5`

**Язык:** C99 и x86-64 ASM

**Лицензия проекта:** MIT

**Целевая платформа:** freestanding x86-64, Multiboot2/Limine, без glibc и без Linux runtime

> **Главное правило.** ATMKoala — не Linux-дистрибутив и не POSIX-система общего назначения. Приложение нельзя собрать против host glibc, скопировать в образ и ожидать, что оно запустится. Каждый компонент должен быть собран для freestanding x86-64 ABI ATMKoala и использовать только реально опубликованные интерфейсы.[1]

## 1. Назначение документа

Это руководство объясняет, как создавать приложения, окна Exp, нативные CPL3-программы, shell-команды, ATPK-пакеты и расширения ядра для ATMKoala. Оно также показывает, какие маршруты **поддерживаются в текущем дереве**, а какие имеются лишь как ABI-задел и пока сознательно возвращают `ENOSYS` или не имеют пользовательского launcher.

Документ предназначен для разработчиков, которые собирают ATMKoala из исходного дерева. Для пользователя готового образа предназначены README и release notes; для автора загружаемого ATPK-пакета есть отдельное руководство.[2]

## 2. Карта путей разработки

| Цель | Практический путь в v0.9 | Изоляция | Состояние |
|---|---|---:|---|
| Новое окно Exp, встроенное в образ | Зарегистрировать `exp_gui_app_t`, добавить `.c` в `Makefile` и вызвать регистрацию в доверенном kernel path | Нет | **Поддерживается** |
| Встроенная shell-команда | Добавить handler в `src/kernel.c` или зарегистрировать trusted SDK command | Нет | **Поддерживается для in-tree кода** |
| Статическая CPL3-программа | Собрать ET_EXEC для ATM ABI; запуск из kernel-side loader/test path | Частичная | **Основание реализовано** |
| Запуск произвольного ELF через shell `exec` | Не использовать | — | **Пока недоступен** |
| Внешнее GUI-приложение через `atm_gui.h` | Можно компилировать против versioned header | Планируется | **Runtime пока возвращает `ENOSYS`** |
| Пакет `.atpk` | Собрать freestanding payload, сформировать ATPK и проверить parser/installer | Зависит от payload | **Базовый путь доступен** |
| Драйвер, ISR, VFS backend | Изменение исходного дерева с полным QA | Нет | **Только доверенный код ядра** |
| Динамически загружаемый универсальный модуль | Не считать стабильным plugin ABI | Нет | **Экспериментально / ограниченно** |

В этом руководстве термин **trusted in-tree** означает код, который компилируется в kernel image и получает тот же адресный простор, что и ядро. Ошибка такого приложения способна повредить ядро, файловую систему или аппаратное состояние. CPL3-программы используют отдельное пользовательское address space, но их публичный запуск и argv/envp pipeline ещё не доведены до полноценного userland.[3]

## 3. Подготовка окружения

Для базовой сборки нужны GNU `gcc`, `as`, `ld`, `objcopy`, Limine sources/tools и утилиты ISO/FAT, используемые Makefile. На Ubuntu-подобном host обычно требуются `build-essential`, `xorriso`, `mtools`, `dosfstools`, `qemu-system-x86`; точный набор зависит от того, собирается ли только kernel или hybrid ISO.

```sh
git clone https://github.com/gosarwi/ATMKoala.git
cd ATMKoala
make all
make iso
```

`make all` создаёт `build/kernel.bin`; `make iso` создаёт canonical Limine BIOS+UEFI hybrid ISO `atmkoala-OS-v0.9-limine.iso`. Основные C flags включают `-ffreestanding`, `-nostdlib`, `-nostdinc`, `-mno-red-zone`, `-fno-pie` и `-mcmodel=kernel`. Не удаляйте эти ограничения из kernel build: они являются частью соглашения между linker script, ранней загрузкой и ядром.[4]

| Команда | Результат | Когда применять |
|---|---|---|
| `make all` | `build/kernel.bin` | После изменения исходников ядра или встроенного приложения |
| `make iso` | Limine hybrid ISO | Перед QEMU и публикацией релиза |
| `make run-limine` | QEMU + Limine ISO | Быстрая интерактивная проверка |
| `make run-full` | QEMU + IDE disk + RTL8139 | Проверка диска и сети в VM |
| `make clean` | Удаляет `build/` и generated image | При подозрении на stale objects |

> **Не меняйте ATMBOOT и ATMUEFI для обычной разработки v0.9.** Основной ISO boot path использует Limine. Эти артефакты рассматриваются как отдельные замороженные compatibility targets.[4]

## 4. Структура репозитория

| Каталог / файл | Назначение |
|---|---|
| `src/` | Kernel, Exp, VFS, драйверы, scheduler и встроенные приложения |
| `sdk/atm_native_abi.h` | Публичный низкоуровневый ABI CPL3: `int $0x80`, номера syscalls и wrappers |
| `sdk/atm_gui.h` | Versioned future-facing GUI ABI; текущий runtime GUI IPC ещё отсутствует |
| `sdk/libc/` | Малый freestanding static libc subset, CRT, linker script и smoke demo |
| `src/exp.h` | Поддерживаемый in-tree API регистрации Exp GUI приложений |
| `src/gui_demo.c` | Малый полный пример зарегистрированного окна Exp |
| `src/ossdk.h` | Trusted SDK hooks, команды, drivers, IRQ, environment и diagnostics |
| `GUIDE_ATPK_DOWNLOADABLE_PACKAGE_RU.md` | Подробности layout, manifest и staging ATPK |
| `tests_qemu_*.sh` | Автоматизированные QEMU regression scripts |
| `boot/limine/` | Конфигурация canonical boot path |

## 5. Быстрый выбор архитектуры приложения

Начните с вопроса: требуется ли приложению настоящее отдельное user process boundary? Если нет, для native desktop utility в текущей версии разумнее создать **in-tree Exp application**. Это единственный путь, который уже даёт окно, draw callbacks, keyboard и pointer input.

Если вы разрабатываете runtime, libc, process manager или планируете будущий независимый userspace, создавайте статический CPL3 ELF. Этот маршрут нужен для проверки ABI, однако не следует обещать конечному пользователю запуск произвольного файла командой `exec`: shell намеренно распознаёт ELF, но не передаёт выполнение в userspace.[5]

```text
Нужен GUI прямо сейчас? ──> In-tree Exp app
Нужна команда в системной shell? ──> Kernel command или trusted SDK command
Нужен sandbox / process / libc experiment? ──> Static ATM-native ELF + kernel-side test path
Нужно скачиваемое распространение? ──> ATPK поверх одного из путей выше
Нужен Linux/X11/SDL binary? ──> Не совместим без отдельного porting layer
```

## 6. In-tree Exp GUI application: рекомендуемый путь

### 6.1. Контракт Exp GUI ABI

`src/exp.h` определяет ABI `EXP_GUI_ABI_MAJOR=1`, `EXP_GUI_ABI_MINOR=0` и лимит `EXP_GUI_MAX_APPS=12`. Регистрация требует непустые `id`, `title` и `draw`; duplicate id, несовместимый major ABI или исчерпанный лимит возвращают ошибку. Callback выполняется в kernel address space, поэтому нельзя считать его sandboxed third-party API.[6]

`exp_gui_context_t` содержит geometry client area, foreground/background palette и необязательное app state. Координаты, передаваемые в `draw`, `key` и `pointer`, являются координатами client area; не рисуйте title bar, border или taskbar самостоятельно.

| Поле / callback | Назначение |
|---|---|
| `id` | Стабильный ASCII идентификатор, например `org.example.notes` |
| `title` | Заголовок окна и launcher text |
| `category` | Группа приложения в UI |
| `default_w`, `default_h` | Предпочтительный размер окна; Exp ограничит его экраном |
| `icon_color` | Базовый RGB color иконки |
| `state` | Указатель на static/heap state, которым владеет приложение |
| `draw(ctx)` | Обязательная отрисовка client area |
| `key(ctx,key,ctrl,alt)` | Необязательный keyboard callback |
| `pointer(ctx,x,y,buttons)` | Необязательный pointer callback |
| `open(ctx)`, `close(ctx)` | Необязательные lifecycle callbacks |

Доступны три helpers: `exp_gui_fill`, `exp_gui_frame` и `exp_gui_text`. Они принимают координаты **внутри client area** и сами учитывают положение Exp window.[6]

### 6.2. Минимальное окно: пример

Создайте `src/hello_app.c`.

```c
#include "exp.h"
#include "util.h"

static int hello_clicks;
static char hello_status[48] = "Ready";

static void hello_draw(exp_gui_context_t *ctx) {
    exp_gui_fill(ctx, 0, 0, ctx->width, ctx->height, ctx->bg);
    exp_gui_fill(ctx, 12, 12, ctx->width - 24, 46, RGB(0x38,0x63,0x80));
    exp_gui_frame(ctx, 12, 12, ctx->width - 24, 46, RGB(0xB0,0xD2,0xE3));
    exp_gui_text(ctx, 24, 26, "Hello, ATMKoala!", RGB(0xFF,0xFF,0xFF),
                 RGB(0x38,0x63,0x80));
    exp_gui_text(ctx, 24, 84, hello_status, ctx->fg, ctx->bg);
}

static void hello_key(exp_gui_context_t *ctx, int key, int ctrl, int alt) {
    (void)ctx; (void)ctrl; (void)alt;
    if (key == 'r' || key == 'R') {
        hello_clicks = 0;
        kstrcpy(hello_status, "Counter reset");
    }
}

static void hello_pointer(exp_gui_context_t *ctx, int x, int y,
                          uint32_t buttons) {
    (void)ctx; (void)x; (void)y; (void)buttons;
    hello_clicks++;
    kstrcpy(hello_status, "Pointer event accepted");
}

static const exp_gui_app_t hello_app = {
    EXP_GUI_ABI_MAJOR, EXP_GUI_ABI_MINOR,
    "org.example.hello", "Hello", "Development",
    420, 220, RGB(0x38,0x63,0x80), NULL,
    hello_draw, hello_key, hello_pointer, NULL, NULL
};

void hello_app_register(void) {
    (void)exp_gui_register(&hello_app);
}
```

Этот шаблон повторяет поддерживаемый in-tree pattern из `src/gui_demo.c`: static state, callbacks, static `exp_gui_app_t` и единственный register function.[7]

### 6.3. Интеграция в образ

Добавьте object в `Makefile` рядом с `build/gui_demo.o`:

```make
build/hello_app.o
```

Затем объявите `void hello_app_register(void);` в подходящем header либо локально в `src/exp.c` и вызовите регистрацию **один раз** при инициализации Exp. Практически это должно быть рядом с существующей встроенной регистрацией demo application; не вызывайте registration в draw loop.

```c
/* после инициализации Exp state, до открытия приложения */
hello_app_register();
```

Соберите ISO, загрузите Exp и выполните:

```text
gui open org.example.hello
```

Команда `gui open <id>` требует активный Exp desktop и вызывает `exp_gui_open()`. Если app id неизвестен или нет свободного window slot, shell выведет ошибку.[8]

### 6.4. Правила отрисовки и input

Каждый вызов `draw` должен полностью рисовать client area, потому что Exp может запросить redraw после перекрытия окна, смены theme, exposure или перемещения. Не храните framebuffer pointer из callback между кадрами. Используйте `ctx->width` и `ctx->height`; не кодируйте размер 800×600.

Keyboard callbacks должны быстро возвращать управление. Pointer callback получает локальные `x,y` и `buttons`; он не является raw PS/2 API. Если обработчик нужен только для click, не делайте busy-wait и не выполняйте disk I/O прямо в draw path.

| Хорошая практика | Почему |
|---|---|
| Перерисовывать всё содержимое в `draw` | Устраняет артефакты после redraw |
| Ограничивать строки и индексы | Экранный renderer и kernel не защищают app от buffer overrun |
| Хранить state в static struct или выделять/освобождать его в lifecycle | Ownership понятен и проверяем |
| Использовать stable reverse-DNS-like id | Предотвращает collision с другими приложениями |
| Ничего не блокировать в callback | Один slow callback портит responsiveness desktop |

### 6.5. Пример stateful callback

Для отдельного app state применяйте явный struct:

```c
typedef struct {
    int counter;
    char message[48];
} sample_state_t;

static sample_state_t state = {0, "Ready"};

static void sample_open(exp_gui_context_t *ctx) {
    ctx->state = &state; /* если state задан в descriptor, Exp передаст его сам */
}
```

На практике `state` задаётся непосредственно в поле `exp_gui_app_t.state`; не присваивайте `ctx->state`, если контекст не объявляет writable ownership. Пример выше показывает model ownership, а правильная интеграция использует descriptor field.

## 7. Встроенная shell-команда

Shell dispatcher находится в `src/kernel.c`. Добавляйте новую встроенную команду только если она относится к базовой системе и её semantic не зависит от неподготовленного external process loader.

```c
else if (!kstrcmp(cmd, "hello")) {
    con_writeln("Hello from an ATMKoala built-in command.");
}
```

После добавления обновите массив command names и section `help`, если командная discoverability важна. Используйте `argc`/`argv`, validate paths через существующий helper `build_abs`, а операции с VFS выполняйте через VFS API. Не вводите `system()`, host file API, `fork()`, `popen()` или Linux ioctl: этих runtime services в kernel нет.

Для trusted in-tree extensions существует `sdk_cmd_register(name, help, fn)`. Такой callback имеет signature `int fn(int argc, char *argv[])`; он регистрируется из доверенного initialization path, а не из произвольного файла пользователя.[9]

```c
#include "ossdk.h"
#include "vga.h"

static int hello_command(int argc, char *argv[]) {
    (void)argc; (void)argv;
    terminal_writeln("hello: trusted SDK command");
    return 0;
}

void hello_command_register(void) {
    (void)sdk_cmd_register("hello-sdk", "print SDK greeting", hello_command);
}
```

> Не выдавайте SDK command registration за защищённый plugin system. В v0.9 callback находится в том же доверенном kernel environment.

## 8. Нативная CPL3-программа и системный ABI

### 8.1. Что реально принимает loader

Kernel-side native loader принимает static x86-64 `ET_EXEC` image, проверенный ELF loader, с размером не больше `ATM_USER_WINDOW_SIZE - ATM_PAGE_SIZE`. Image получает новый user address space, entry point, user stack и initial `brk`. Начальный stack намеренно минимален: `argc=0`, `argv=NULL`, `envp=NULL` и нулевой auxv terminator. Поэтому приложение не должно ожидать arguments, environment, dynamic loader, TLS, signals или Linux `/proc` semantics.[3]

| Свойство | Текущее поведение |
|---|---|
| Формат | Static ELF64 `ET_EXEC` для x86-64 |
| Load base | Native linker script размещает образ от `0x40000000` |
| Dynamic linking | Нет |
| `argc`, `argv`, `envp` | `0`, `NULL`, `NULL` |
| User process entry | Реализован через scheduler task + CPL3 gate |
| Shell `exec file` | Намеренно не запускает ELF в v0.9 |
| Проверенный пример | Встроенный libc smoke ELF и native loader self-test |

### 8.2. Соглашение syscall

System call выполняется как `int $0x80`: номер в `RAX`, первые три аргумента в `RDI`, `RSI`, `RDX`, return value в `RAX`. Публичный header уже содержит inline wrappers, поэтому вручную писать inline ASM обычно не требуется.[1]

| Группа | Доступные опубликованные номера |
|---|---|
| Файлы | `READ=0`, `WRITE=1`, `OPEN=2`, `CLOSE=3`, `FSTAT=5`, `LSEEK=8` |
| Process / identity | `BRK=12`, `EXIT=60`, `WAITPID=61`, `KILL=62`, `GETPID=39`, `GETPPID=110`, `GETUID=102`, `GETGID=104`, `GETTID=186` |
| ABI | `ATM_SYS_ABI_INFO=0xA700` |
| Sockets | `SOCKET=0xA710`, `CONNECT=0xA711`, `BIND=0xA712`, `LISTEN=0xA713`, `ACCEPT=0xA714` |

Ошибки возвращаются как non-negative result или отрицательный ATM errno-style value. `ATM_EFAULT`, `ATM_ENOMEM`, `ATM_EINVAL` и `ATM_ENOSYS` определены в public header. Не смешивайте эти значения с host `errno` без явной translation policy.[1]

### 8.3. Минимальный freestanding source

```c
#include "atm_native_abi.h"

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    static const char message[] = "Hello from CPL3 ATMKoala app\\n";
    int64_t rc = atm_write(1, message, sizeof(message) - 1);
    return rc < 0 ? 1 : 42;
}
```

Собирайте static image с project CRT, libc subset и linker script, а не с system libc. Конкретный verified build pattern находится в Makefile target `build/libc_smoke.elf`: CRT AS source, freestanding C objects, `ld -m elf_x86_64 -T sdk/libc/atm_native.ld` и отказ от dynamic loader.[4]

```sh
# Схема, а не copy-paste универсальный build system.
as --64 sdk/libc/src/crt0.s -o build/app-crt0.o
gcc <freestanding-CFLAGS> -I sdk -I sdk/libc/include -c app.c -o build/app.o
ld -m elf_x86_64 -T sdk/libc/atm_native.ld \
   build/app-crt0.o build/app.o <libc-objects> -o build/app.elf
```

Точный набор `<libc-objects>` зависит от API, которое вызывает программа. Безопаснее скопировать и адаптировать target `LIBC_SMOKE_ELF`, чем пытаться линковать `-lc` host system.[4]

### 8.4. Малый libc subset

В `sdk/libc` доступна freestanding static библиотека для части строк, ctype, stdlib, heap, unbuffered stdio, файловых wrapper и socket wrapper. Встроенный smoke app проверяет `calloc/realloc/free`, string operations, ASCII ctype, `strtol/strtoull`, `fstat`, `socket/close` и `write`.[10]

Не считайте этот subset полной musl или полной POSIX libc. Dynamic loader, pthread/TLS, full locale database, wide-character stack, general resolver, full stdio semantics, Linux ABI и плавающая точка не обещаются этим интерфейсом.

## 9. GUI ABI для будущих внешних клиентов

`sdk/atm_gui.h` уже фиксирует layouts handles, window descriptor, surface descriptor и event structure для ABI v1. Однако в v0.9 user-mode GUI IPC/compositor endpoint отсутствует. `atm_gui_runtime_info()` сообщает versioned ABI и zero capabilities; create/present/event functions возвращают `-1` с `ENOSYS`.[11]

Поэтому допустимо компилировать код против header, выделять feature flag и корректно отказаться от GUI:

```c
#include "atm_gui.h"

int app_try_gui(void) {
    atm_gui_runtime_info_t info = {
        .struct_size = sizeof(info)
    };
    if (atm_gui_runtime_info(&info) < 0) return -1;
    if (!(info.capabilities & ATM_GUI_CAP_WINDOWS)) return -1;
    /* future window/surface path */
    return 0;
}
```

Нельзя писать приложение, которое трактует `atm_gui_window_create()` как доступную функцию v0.9. Для работающего GUI используйте in-tree Exp route из раздела 6.

## 10. Данные, файлы и VFS

Используйте VFS paths и file descriptors вместо доступа к disk sectors из обычного приложения. CatFS — основной writable mounted data path; ext2 adapter и другие formats имеют свои ограничения. Application data следует хранить под отдельным predictable path, например `/data/apps/<package-id>/`, и ограничивать имя/размер каждого файла.

Не считайте права POSIX, ownership, symlink semantics или cross-filesystem atomicity полностью реализованными. Перед `rename`, `unlink`, create или install проверяйте текущую поддержку конкретного backend. Для пользовательских документов Exp Files уже использует Trash path `/data/uiu/Trash`; не подменяйте его raw deletion logic в UI app без явного design review.

## 11. ATPK: распространение приложения

ATPK — нативный package format ATMKoala, а не `.deb` и не контейнер для glibc Linux binary. Standard layout содержит `ATPK/control`, `ATPK/manifest`, декларативный `ATPK/hooks` и `data/` payload. Manifest описывает relative path, byte size и CRC32 для каждого data file.[2]

```text
weather-package/
├── src/weather.c
├── build/weather.elf
├── package/
│   ├── ATPK/control
│   ├── ATPK/manifest
│   └── data/apps/weather/weather.elf
└── SHA256SUMS
```

Минимальный `ATPK/control`:

```text
Package: weather
Version: 1.0.0
Architecture: atmkoala-x86_64
Depends: exp>=0.5
Maintainer: Example Team
Description: Example weather application
Entry: apps/weather/weather.elf
License: MIT
```

Проверяйте, что все archive paths relative, не содержат `..`, не повторяются и соответствуют manifest. Не включайте device nodes, raw disk operations, maintainer scripts, host binaries, private keys, `.git/` или build artifacts. Встроенная shell command имеет базовый flow:

```text
pkg create weather /apps/weather/weather.elf
pkg info weather.atpk
pkg install weather.atpk
pkg list
```

`pkg create` — удобный базовый builder для простого payload; для production package author должен проверять окончательный manifest и policy согласно отдельному ATPK guide. Installer делает staging/verification path, но текущий package system нельзя объявлять полноценным transactional package manager.[2] [5]

## 12. Драйверы, filesystem extensions и kernel services

`ossdk.h` публикует структуру `sdk_driver_t`, hooks, IRQ install API, memory info, module descriptors, themes, serial output и CPUID helper. Это полезно при разработке **доверенного in-tree** кода, но не образует безопасный public driver marketplace.[9]

| Тип расширения | Минимальный безопасный подход |
|---|---|
| Новый driver | Сначала PCI/port/MMIO discovery, затем bounded init, затем QEMU fixture и diagnostics command |
| Новый VFS backend | Начать с read-only probe и mount validation; только затем add write semantics |
| Новый filesystem writer | Проектировать metadata consistency, rollback и crash behavior до advertising write support |
| IRQ handler | Минимум работы в handler; deferred work и строгая проверка IRQ ownership |
| Network driver | Не утверждать operational state по одному PCI detection; разделять detected, initialized и packet-tested |
| GPU support | Сначала inventory/framebuffer status; не называть VBE или TinyGL hardware acceleration |

Для driver changes обязательны serial diagnostics, hardware-safe failure path и QEMU regression. Команды `gpu`, `net drivers`, `mouse` и `swap` в текущем проекте специально различают detection от operational driver. Следуйте этому стилю во всех новых diagnostics.

## 13. Internationalization, theme и UX

Язык и theme являются системными настройками Exp. Новое приложение должно использовать `ctx->fg`, `ctx->bg` и существующие Exp drawing helpers, а не предполагать фиксированный белый/чёрный фон. Все строки должны быть bounded и ASCII-safe, пока не проверено конкретное font/Unicode rendering path.

Timezone хранится как IANA-style identifier, но wall-clock conversion пока не производится без RTC/NTP. Поэтому приложение может показать выбранный id, например `Europe/Moscow`, но не должно вычислять или обещать точное local time исключительно на основании этой строки.

## 14. Тестирование и отладка

### 14.1. Минимальный gate перед commit

| Уровень | Проверка | Ожидаемый результат |
|---|---|---|
| Compile | `make all` | `build/kernel.bin` без errors |
| Artifact | `make iso` | Hybrid ISO создан |
| POSIX subset | QEMU + `posix test` | Project self-test показывает `OK` |
| Exp GUI | Открыть app, изменить окно, keyboard и pointer input | Нет crash, redraw корректен |
| Storage | Disposable disk image | Нет тестов на ценных носителях |
| Package | `pkg info`, затем staged install | Manifest проверен, нет path traversal |
| Native ABI | Existing native/libc selftests | Процесс exit/reap проходит |

Уже присутствующие QEMU scripts являются образцами automation: `tests_qemu_scale_gui_posix.sh`, `tests_qemu_installer_boot.sh`, `tests_qemu_new_command_surfaces.sh` и `tests_qemu_calculator_nano.sh`. Применяйте disposable disk images для всего, что может писать partition table, CatFS или package payload.[12]

### 14.2. Практический QEMU запуск

```sh
# Минимальный VBE/Exp запуск
qemu-system-x86_64 -m 256M -vga std -cdrom atmkoala-OS-v0.9-limine.iso

# Полный IDE + RTL8139 fixture
make run-full
```

Для automated visual regression QEMU monitor умеет отправлять key events и делать `screendump`; текущие test scripts используют именно этот подход. Screenshot нужно проверять по содержанию, а не только по exit code shell script, если тест утверждает UI behavior.

### 14.3. Panic-safe правила

Не проверяйте new driver на единственном рабочем physical disk. Не вызывайте raw write из graphics callback. Не помещайте parsing untrusted archive/network data в IRQ context. При эксперименте с loader, ELF или paging добавляйте bounded image size, explicit validation и serial messages до risky instruction path.

## 15. Что пока не следует обещать пользователям

| Возможность | Честный статус v0.9 |
|---|---|
| Полная POSIX совместимость | Нет; есть tested project subset |
| glibc / запуск Linux ELF | Нет |
| Dynamic linker / shared libraries | Нет |
| Запуск ELF через shell `exec` | Нет, намеренно заблокирован |
| External GUI IPC | ABI layout есть, runtime возвращает `ENOSYS` |
| Hardware OpenGL/Mesa/GLX | Нет; TinyGL-Lite — CPU renderer |
| GPU acceleration | Нет; есть PCI/VBE diagnostics |
| Swap | Есть inventory MBR type `0x82`; pager, activation и eviction отсутствуют |
| TLS/HTTPS | Нет |
| USB mass storage, AHCI, NVMe | Нет как usable block-driver path |
| Safe transactional ext2/btrfs write | Не заявляется |

Этот список должен быть частью README приложения, package description и release notes, если программа зависит от соответствующего ограничения. Документация, которая объявляет это готовым, считается ошибкой документации.

## 16. Checklist автора приложения

Перед pull request или публикацией package ответьте на следующие вопросы полными ответами.

| Вопрос | Требуемый ответ |
|---|---|
| Какой путь используется? | Exp in-tree, trusted command, CPL3 experiment или ATPK |
| Есть ли host dependency? | Нет glibc, dynamic loader, Linux syscalls или host assets at runtime |
| Есть ли unbounded input? | Нет; все buffers, path и loops ограничены |
| Верно ли описан GUI? | Exp callback app либо future `ENOSYS` client, не смешаны |
| Есть ли recovery? | Failure path не пишет destructive state без confirmation |
| Проверен ли QEMU? | Да, с reproduce command/script и expected UI/serial result |
| Проверен ли package? | `pkg info` и install staged on disposable data |
| Обновлена ли документация? | README/package description/limitations соответствуют коду |

## 17. Рекомендуемый порядок работы

1. Выберите один путь из раздела 2 и оформите короткий design note.
2. Сделайте маленький working vertical slice: одно окно, одна command или один syscall.
3. Добавьте bounds checks и visible diagnostics до расширения functionality.
4. Соберите kernel и ISO на чистом build path.
5. Проверьте QEMU на disposable image.
6. Добавьте regression script или расширьте существующий.
7. Напишите package metadata и user-facing limitations.
8. Только после этого публикуйте commit, ISO или ATPK.

Такой порядок предпочтительнее крупного «порта» Linux-программы: он фиксирует ABI boundary, делает failures наблюдаемыми и не выдаёт желаемые возможности за уже работающий runtime.

## References

[1]: https://github.com/gosarwi/ATMKoala/blob/main/sdk/atm_native_abi.h "ATM Native App ABI v1"
[2]: https://github.com/gosarwi/ATMKoala/blob/main/GUIDE_ATPK_DOWNLOADABLE_PACKAGE_RU.md "ATPK package author guide"
[3]: https://github.com/gosarwi/ATMKoala/blob/main/src/native_app.c "Native CPL3 application loader"
[4]: https://github.com/gosarwi/ATMKoala/blob/main/Makefile "Canonical freestanding build and Limine ISO targets"
[5]: https://github.com/gosarwi/ATMKoala/blob/main/src/kernel.c "Shell package and exec handlers"
[6]: https://github.com/gosarwi/ATMKoala/blob/main/src/exp.h "Exp in-tree GUI ABI v1"
[7]: https://github.com/gosarwi/ATMKoala/blob/main/src/gui_demo.c "Complete Exp GUI registration example"
[8]: https://github.com/gosarwi/ATMKoala/blob/main/src/exp.c "Exp registration and gui open implementation"
[9]: https://github.com/gosarwi/ATMKoala/blob/main/src/ossdk.h "Trusted in-tree SDK interfaces"
[10]: https://github.com/gosarwi/ATMKoala/blob/main/sdk/libc/demo/libc_smoke.c "Freestanding libc smoke application"
[11]: https://github.com/gosarwi/ATMKoala/blob/main/sdk/atm_gui.h "Future external GUI ABI contract"
[12]: https://github.com/gosarwi/ATMKoala/tree/main "ATMKoala QEMU regression scripts"
