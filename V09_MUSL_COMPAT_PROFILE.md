# ATMKoala v0.9 — musl-compatible native libc profile

**Решение:** ATMKoala не будет использовать glibc. Userspace строится как **собственный статический native libc runtime**, совместимый по стилю и по выборочным API с musl. Там, где исходный код musl действительно переносится, сохраняются требуемые copyright/license notices. Цель — не Linux binary compatibility, а компактный MIT-совместимый C/POSIX foundation для ATM native applications.

> musl itself is an implementation of ISO C and POSIX interfaces built atop the Linux system-call API; ATMKoala заменяет этот нижний Linux syscall dependency собственным versioned ATM Native App ABI.[1]

## Licensing decision

musl как проект распространяется под standard MIT license.[2] Его official copyright file также перечисляет отдельные third-party portions под совместимыми permissive licenses, включая BSD-2-Clause и public-domain-derived code.[2] Следовательно, **MIT license самого проекта ATMKoala не является причиной избегать musl**. Однако каждая выбранная upstream file должна пройти attribution review: нельзя автоматически заявлять, что все строки дерева musl являются только MIT.

| Материал | Допустимый подход в ATMKoala | Обязательство |
|---|---|---|
| Новый ATMKoala code | MIT. | Copyright header проекта. |
| musl source под MIT | Адаптация после технического review. | Preserve notice/permission text where required. |
| musl portions под BSD-2-Clause | Допустимая адаптация. | Preserve BSD notice. |
| Other permissive source | Только после file-level verification. | Record provenance in `THIRD_PARTY_NOTICES.md`. |
| glibc source | Не используется. | Неприменимо. |

## v0.9 libc contract

Первая libc не копирует полный Linux environment. Она компилируется статически вместе с native application, использует `sdk/atm_native_abi.h`, не зависит от dynamic loader и не требует Linux `syscall` numbers beyond explicitly shared conventional values.

| Компонент | v0.9 target | Текущая база |
|---|---|---|
| CRT | `_start`, ABI-safe stack decoding, `main`, `exit`. | Static ELF loader + argc/argv/envp placeholder stack. |
| Error model | Thread-locality deferred; single-task `errno` storage first. | Negative ATM errno-style results at syscall boundary. |
| Memory | `malloc`/`free` on top of `brk`; no `mmap` yet. | Page-backed bounded `brk`. |
| C core | `mem*`, `str*`, integer conversion, `qsort` subset. | Kernel helpers exist but are not a public userspace libc. |
| POSIX I/O | `open`, `close`, `read`, `write`, `lseek`, `fstat`. | Task-local FD namespace plus checked user-copy. |
| stdio | Minimal unbuffered/buffered `FILE` pilot after I/O. | Deferred. |
| Threads/TLS | Deferred. | No user preemption/TLS contract yet. |
| Dynamic linking | Deferred. | Static `ET_EXEC` only. |

## What may be reused first

The safest first candidates are isolated, pure C routines whose input/output contract does not assume Linux kernel or global musl internals: selected string routines, integer conversion, UTF-8 helpers, `qsort`, `bsearch`, and portions of `stdio` after the ATM `FILE` layer exists. CRT code is also a strong candidate for design reference: musl’s porting guidance explicitly identifies a C-based CRT framework as a simplification for architecture-specific startup work.[3]

Code requiring Linux-specific syscall wrappers, `clone`, `futex`, `mmap`, signals, `/proc`, dynamic linker internals, NPTL threading or ELF `PT_INTERP` is deliberately out of scope until the corresponding ATM ABI feature exists.

## Rules for compatibility claims

1. Say **“musl-compatible subset”** only when an API’s documented semantics and tests match the selected target.
2. Say **“musl-derived”** only when source provenance and notices are recorded.
3. Do not claim musl, Linux, POSIX or LXQt binary compatibility merely because a function shares a name.
4. Keep ATM-specific ABI services in reserved native namespaces, never silently overload Linux semantics.

## References

[1] [musl: About](https://musl.libc.org/about.html)

[2] [musl: COPYRIGHT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT)

[3] [musl wiki: Porting](https://wiki.musl-libc.org/porting)
