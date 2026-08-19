# ATMKoala v0.9 — ATM Native App ABI v1

**Статус:** утверждённый профиль реализации v0.9.  
**Архитектура:** x86-64 long mode, LP64, little-endian.  
**Назначение:** безопасное выполнение статически слинкованных нативных приложений в CPL 3 и стабильная основа для собственного GUI.

## 1. Границы совместимости

ATM Native App ABI v1 использует знакомые POSIX и Linux-подобные имена там, где это помогает переносить исходный код. Он не является Linux kernel ABI и не обещает запуск бинарников, собранных для Linux. В v1 не поддерживаются ELF interpreter (`PT_INTERP`), ELF shared objects (`ET_DYN`), runtime relocation, TLS, `fork()`, `clone()`, `pthread`, Unix sockets, D-Bus, DRM, EGL, X11, Wayland или Qt.

> Целевой исполняемый формат v1 — статически слинкованный **ELF64 ET_EXEC** с `EM_X86_64` и loadable `PT_LOAD` сегментами. Динамические приложения отклоняются до появления отдельного dynamic linker milestone.

Такой выбор делает ABI достижимым в freestanding ОС: kernel должен построить из файла изолированное адресное пространство, подготовить стек, войти в CPL 3 и предоставить узкий набор системных сервисов. Полноценный Linux compatibility layer или LXQt остаются отдельными поздними направлениями.

## 2. Процесс и начальный контекст

Каждое приложение имеет собственный `user_space_t`, PID, PPID, UID, GID, `cwd`, `umask`, environment и таблицу файловых дескрипторов. Scheduler task хранит ссылку на user process и переключает CR3 при передаче выполнения. Kernel stack принадлежит задаче и не отображается пользователю.

Начальная точка ELF получает System V-подобный stack layout. Вершина стека выровнена на 16 bytes. По адресу `RSP` находятся `argc`, затем `argv[]`, zero sentinel, `envp[]`, zero sentinel, после чего могут следовать private auxiliary entries. В v1 обязательны только `argc`, `argv` и `envp`; auxiliary vector не является публичным контрактом.

| Свойство | Контракт v1 |
|---|---|
| Выполняемый файл | ELF64 `ET_EXEC`, `EM_X86_64`, static only. |
| Код и данные | Только корректные, непересекающиеся `PT_LOAD`; `p_filesz <= p_memsz`, BSS zero-filled. |
| Стек | Одна выделенная user stack область в initial milestone, 16-byte aligned. |
| Аргументы | `argc >= 0`, `argv[argc] == NULL`, `envp` завершён `NULL`. |
| Точка завершения | `main()` возвращает статус через CRT stub или вызывает `exit`. |
| Ошибка загрузки | `execve`/spawn возвращает отрицательный ATM errno; процесс не создаётся. |

## 3. Системный ABI

Вызов v1 выполняется через уже существующий interrupt gate `int $0x80`. До отдельного скоростного milestone `SYSCALL/SYSRET` не используется: interrupt gate проще безопасно проверить при раннем переходе к CPL 3. Регистровый контракт фиксирован: `RAX` — number, `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9` — arguments; `RAX` — non-negative result или `-ATM_E*` при ошибке. Все адреса, диапазоны и output buffers от CPL 3 проходят `copy_from_user`, `copy_to_user` либо проверенное mapping-aware чтение/запись.

C runtime wrapper преобразует отрицательный результат в `-1`, записывает положительный код в task-local `errno` и возвращает результат в привычной POSIX форме. Низкоуровневый asm syscall wrapper не делает такого преобразования.

| Группа | Обязательна для v0.9 baseline | Следующий этап |
|---|---|---|
| Информация | `ABI_INFO`, `getpid`, `getppid`, `getuid`, `getgid`, `gettid`, `clock_gettime`. | `uname`, resource limits. |
| Файлы и fd | `open`, `close`, `read`, `write`, `lseek`, `fstat`, `dup`, `dup2`, `fsync`. | `readv`, `writev`, `fcntl`, directories. |
| Пути | `chdir`, `getcwd`, `mkdir`, `unlink`, `rename`, `access`. | symlinks, chmod/chown policy. |
| Процессы | `exit`, `waitpid`, cooperative `kill`, static `execve`/spawn. | signals, job control, `fork`. |
| Память | `brk`, anonymous private `mmap`, `munmap`. | file mapping, shared memory, demand paging. |
| События | blocking read on supported handles и `poll`-like native event wait. | pipes, eventfd, GUI shared buffers. |

Существующие Linux x86-64-compatible numbers сохраняются, если они уже опубликованы: `read=0`, `write=1`, `open=2`, `close=3`, `lseek=8`, `exit=60`, `waitpid=61`, `kill=62`, `getpid=39`, `getuid=102`, `getgid=104`, `gettid=186`. Вновь добавляемые native services получают reserved ATM range, если совпадение с Linux создало бы ложное обещание семантической совместимости.

## 4. User memory и безопасность

Kernel не принимает любой integer как pointer. Для каждой операции с user buffer выполняются: проверка `NULL` policy, overflow-safe `address + size`, проверка пределов `ATM_USER_BASE..ATM_USER_TOP`, проверка доступных PTE страниц и разделение read/write permissions. Нулевая длина разрешается там, где это допускает POSIX semantics. Невалидный адрес возвращает `-ATM_EFAULT` без kernel panic.

`copy_from_user()` копирует в kernel-owned temporary storage перед VFS и scheduler операциями. `copy_to_user()` используется для outputs, таких как `stat`, `getcwd`, `waitpid status` и event records. Этот boundary является обязательным до включения любого CPL 3 file I/O.

## 5. POSIX source profile

Публичные C headers предоставят `atm_posix_*` как kernel-internal implementation detail и `atm/unistd.h`, `atm/fcntl.h`, `atm/errno.h`, `atm/sys/stat.h`, `atm/time.h` для нативных приложений. Стандартные имена допустимы внутри toolchain sysroot, но ABI будет маркирован `ATM_NATIVE_ABI_V1`, чтобы исключить смешение с Linux headers и Linux libc.

Первый GUI toolkit использует только baseline profile: file handles, time, memory, event wait и native AWM client calls. Это позволяет перенести Exp components из kernel context постепенно и исключает зависимость от Qt или Mesa на фундаментальном этапе.

## 6. Версионирование

`ATM_SYS_ABI_INFO` возвращает immutable manifest: major/minor ABI version, supported feature bitmap, pointer width, page size, ELF profile и GUI protocol range. ABI v1 допускает добавление новых syscall numbers и feature bits без изменения существующей семантики. Изменение структуры, номера, register contract или результата существующего syscall требует нового major ABI.

| Manifest field | Назначение |
|---|---|
| `major`, `minor` | Выбор compatible client path. |
| `features` | Проверка optional file, memory, process и GUI capabilities. |
| `page_size`, `user_base`, `user_top` | Валидация allocator и map requests. |
| `elf_flags` | Static ELF и future dynamic-loader capability. |
| `gui_protocol_major/minor` | Привязка GUI client библиотеки к AWM server. |

## 7. Последовательность v0.9

Первым изменением является не добавление десятков POSIX wrappers, а завершение безопасной вертикали `static ELF → user process → int 0x80 → user-copy → fd operation → exit/wait`. После её QEMU regression можно расширять memory, path и event layers. Только затем AWM получает user-client protocol и буферы, нужные полноценному GUI.

## References

[1] [The Open Group Base Specifications Issue 7, POSIX System Interfaces](https://pubs.opengroup.org/onlinepubs/9699919799.2018edition/)

[2] [Linux man-pages: syscalls(2)](https://man7.org/linux/man-pages/man2/syscalls.2.html)

[3] [ELF gABI: Program Loading](https://gabi.xinuos.com/elf/07-pheader.html)

[4] [System V Application Binary Interface: AMD64 Architecture Processor Supplement](https://refspecs.linuxbase.org/elf/x86_64-abi-0.98.pdf)
