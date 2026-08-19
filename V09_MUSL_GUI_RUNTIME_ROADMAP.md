# ATMKoala v0.9 — путь к musl-compatible libc и native GUI runtime

## Principle

ATMKoala continues with a compact **static native libc** rather than glibc. musl is a useful design and selective-source reference because it is MIT-licensed and designed around small, self-contained components, but its ordinary implementation is built over the Linux syscall API.[1] [2] ATMKoala therefore ports interfaces only after the native kernel ABI supplies the necessary semantics.

> The target is **source-level API compatibility where tested**, not an unsupported claim that Linux musl, Qt, Mesa or LXQt binaries can run unchanged.

## Current completed floor

The current source tree proves a static x86-64 `ET_EXEC` application in CPL 3 with custom CRT, C `main`, a brk-backed allocator, independent memory/string routines, `errno`, `open`/`close`/`read`/`write`, and process-owned `/dev/tty` stdio descriptors. The test application is built with `-nostdlib` and returns through the real `exit`/`waitpid` path.

| Availability | Native v0.9 component | GUI value |
|---|---|---|
| Ready | Static ELF loader, separate address space, C ABI, CRT. | Execute a GUI client as an isolated process. |
| Ready | `brk`, allocator, `errno`, strings, unbuffered descriptor I/O. | Allocate widget data and write diagnostics. |
| Ready | Process-owned `/dev/tty` and checked user buffers. | Separate application handles from kernel VFS state. |
| Deferred | `mmap`, shared mapping, poll/select, time, signals, threads/TLS. | Required before robust compositor buffers and mature toolkit. |
| Deferred | `ET_DYN`, relocations, `PT_INTERP`, `dlopen`. | Required before shared native GUI libraries or any Mesa-style port. |

## Selective musl-adaptation order

| Stage | Candidate component class | Required ATM precondition | License/provenance rule |
|---|---|---|---|
| **M0** | Pure C strings, integer conversion, sort/search, UTF-8 helpers. | None beyond current static libc. | Prefer project implementations; if adapted, record exact musl path and notice. |
| **M1** | Buffered stdio and robust `printf` subset. | Stable descriptor behaviour, error mapping and `lseek`. | Preserve source-level copyright notices. |
| **M2** | Filesystem/path and time helpers. | `stat`, `getdents`/directory ABI, clock API. | No code whose Linux syscall dependency is unresolved. |
| **M3** | Thread primitives and TLS-facing libc. | Futex-like wait, thread creation, TLS + guarded stacks. | No `pthread` claim before race and cancellation tests. |
| **M4** | Dynamic loader-related code. | `mmap`, `ET_DYN`, RELA, `PT_INTERP`, auxv, symbol rules. | Treat loader as a separately audited subsystem. |

The official musl porting page identifies CRT as an architecture-sensitive part of a port and recommends using a dedicated libc test suite.[3] ATMKoala already separated its CRT into `sdk/libc/src/crt0.s` and `crt.c`; the next test work should extend the independent smoke app before importing any large upstream component.

## Native GUI runtime path

The first graphical platform must **not** wait for Mesa or LXQt. It will be static and native over AWM/Exp.

| GUI milestone | Kernel/libc gate | Deliverable |
|---|---|---|
| **G0: client ABI** | Current static libc. | `sdk/atm_gui.h`: ABI version, window/surface handles, events and error encoding. |
| **G1: compositor service** | Stable static processes and pipe/event primitive. | AWM-owned compositor task with application isolation. |
| **G2: surface buffers** | `mmap` or verified shared-memory object API. | Double-buffered client surfaces, damage rectangles and present fence. |
| **G3: toolkit** | Font/image services and input events. | Native buttons, text fields, lists and accessible focus semantics. |
| **G4: software graphics** | Surface allocator + TinyGL Lite binding. | 2D/3D software context targeting an AWM surface. |
| **G5: portability layer** | Dynamic linking, threads, POSIX event/runtime depth. | Limited EGL/OpenGL-like native facade; then reassess selective Mesa code. |

## Explicit non-goals for this milestone

The project does not import glibc. It does not yet import a full musl tree, a Linux dynamic linker, or Qt/LXQt. It does not declare Wayland/X11 protocol compatibility. Those are later integration decisions, not substitutes for defining a stable ATM window/event ABI now.

## References

[1] [musl: About](https://musl.libc.org/about.html)

[2] [musl: COPYRIGHT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT)

[3] [musl wiki: Porting](https://wiki.musl-libc.org/porting)
