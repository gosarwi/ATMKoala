# ATMKoala v0.9 — roadmap dynamic linking и GUI-ready ABI

**Статус:** архитектурный roadmap; не является заявлением о поддержке shared libraries в v0.9 baseline.

## Текущий executable profile

ATMKoala v0.9 запускает только **static ELF64 ET_EXEC** для x86-64. Loader проверяет `PT_LOAD` segments, отображает их в отдельное `user_space_t`, zero-fills BSS, создаёт user stack и передаёт выполнение через CPL 3. Это соответствует базовой модели ELF program loading: segment содержит file bytes размером `p_filesz` и zero-filled tail до `p_memsz`.[1]

Нативный ABI v1 намеренно не принимает `PT_INTERP`, `ET_DYN`, `DT_NEEDED`, relocation sections или thread-local storage. Отсутствие этих компонентов означает, что Linux shared objects, glibc, Qt, Mesa или LXQt binaries **не могут и не должны** запускаться на текущем этапе.

| Уровень | v0.9 baseline | После baseline |
|---|---|---|
| Формат | ELF64 `ET_EXEC`, static. | `ET_DYN`, PIE и shared objects. |
| Загрузка | `PT_LOAD`, BSS, fixed native window. | Program interpreter, dependency graph, symbol resolution. |
| Память | Fixed user window, stack, `brk` private heap. | `mmap`, guarded stacks, file maps, shared mappings. |
| Runtime | Raw syscall header и application-owned CRT. | Native libc subset, `errno`, startup CRT, allocator. |
| Параллелизм | Один executing user context per task. | TLS, futex-like wait, threads. |
| GUI | Future native AWM client ABI. | Shared surface buffers, event handles, UI toolkit. |

## Почему dynamic linker нельзя добавлять первым

Dynamic loading предполагает целостную цепочку гарантий: loader должен безопасно читать program headers, создавать mappings с правильными permissions, размещать и применять relocations, обслуживать symbol scopes, передавать control в interpreter и обеспечивать lifecycle shared pages. Он также требует устойчивого process memory API, потому что библиотеки должны размещаться независимо от main executable. GABI отдельно описывает program loading и dynamic linking как разные части формата; первая часть уже покрывает v0.9 static profile, вторая остаётся будущей работой.[1]

Поэтому первый GUI ATMKoala должен использовать static native applications и versioned AWM client library. Это позволяет решить compositing, surfaces, event loop и toolkit без блокировки на loader/libc/TLS. Mesa software integration также начинается с нативного static renderer backend, а не с Linux Mesa binary.

## Порядок будущих milestone

| Milestone | Обязательные изменения | Получаемая возможность |
|---|---|---|
| **DL0: v0.9 static ABI** | Завершённые `execve` arguments, process-owned FDs, `brk`, syscall manifest, AWM client protocol. | Сторонние статические CLI и GUI-приложения. |
| **DL1: memory runtime** | `mmap`, `munmap`, file-backed mappings, guard pages, `auxv`, reliable page reclamation. | Компактная native libc и изолированные heaps. |
| **DL2: dynamic ELF core** | `ET_DYN`, `PT_INTERP`, `.dynamic`, RELA relative relocations, read-only shared text mappings. | Одно native shared library и PIE pilot. |
| **DL3: libc/thread substrate** | Symbol lookup, `DT_NEEDED`, TLS, errno ABI, futex-like primitive, dynamic startup. | Native libraries и threaded GUI runtime. |
| **DL4: graphics portability** | Software GL/EGL-like native layer, pixel-buffer handles, image/video allocator policy. | TinyGL-based 3D apps и первая Mesa-oriented compatibility facade. |
| **DL5: broader ports** | POSIX IPC/network/process depth, C++ runtime, XML/DBus substitutes or ports, full GUI toolkit policy. | Реалистичная оценка отдельных Qt/LXQt components. |

## ABI stability policy

`ATM_NATIVE_ABI_MAJOR=1` остаётся stable для published syscall numbers, register convention, pointer width and error encoding. Новые optional calls добавляются только через feature bits. Переназначение существующего number, изменение argument structure или binary meaning требует нового major ABI. Такая политика важна потому, что system call является boundary между приложением и ядром; wrapper/runtime отвечает за переносимость semantics и представление ошибок.[2]

Стабильность не равна имитации Linux. Linux-compatible values используются только там, где ATMKoala действительно обеспечивает заявленную базовую семантику. Native-specific services получают отдельный reserved range и собственную документацию.

## Результат для GUI и Mesa

Цель v0.9 — не «запустить LXQt немедленно», а создать хороший фундамент, на котором собственный GUI будет сильнее и проще: statically linked native applications, process isolation, stable event protocol and compositor surfaces. Когда этот уровень станет зрелым, TinyGL Lite можно оформить как native software-rendering backend. Затем появится ограниченная Mesa-oriented facade; только существенно позже имеет смысл оценивать полноценную Mesa или Qt/LXQt port.

## References

[1] [ELF gABI: Program Loading](https://gabi.xinuos.com/elf/07-pheader.html)

[2] [Linux man-pages: syscalls(2)](https://man7.org/linux/man-pages/man2/syscalls.2.html)

[3] [System V Application Binary Interface: AMD64 Architecture Processor Supplement](https://refspecs.linuxbase.org/elf/x86_64-abi-0.98.pdf)
