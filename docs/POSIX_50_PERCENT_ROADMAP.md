# ATMKoala POSIX 50% Roadmap and Milestone Record

**Status:** The local v0.9 milestone is complete at an estimated **approximately 50%** of ATMKoala’s selected practical POSIX-shaped source profile. The estimate is a project planning measure, not POSIX certification, Linux binary compatibility, BusyBox/Toybox compatibility, or musl readiness.

> **Counting rule.** An interface contributes only when its semantics, failure cases and bounds are implemented; the static runtime exposes it where appropriate; checked CPL3 code executes it; and QEMU regression evidence exists. A declaration, stub, or placeholder success return contributes zero.

## Completed gates

| Gate | Completed scope | Verified outcome |
|---:|---|---|
| 1 | Build hygiene and regression baseline | Local rebuild, generated ISO, automatic C header dependencies, and QEMU regression harness remain operational. |
| 2 | Scheduler-safe runtime sleep | `nanosleep`, `sleep`, and `usleep` use private scheduler deadline metadata and PIT wakeup. Requests are validated and rounded upward to the 100 Hz tick. |
| 3 | Descriptor runtime | `F_DUPFD`, `F_GETFL`, `F_SETFL`, `F_GETFD`, `F_SETFD`, `FD_CLOEXEC`, `O_CLOEXEC`, access-mode-preserving flags, pipes and finite pipe-only `poll`/`select` waits are covered. Descriptor close-on-exec state is deliberately private to the descriptor layer rather than enlarged task metadata. |
| 4 | Initial user process ABI | Static native ELF entry receives a bounded vector stack containing `argc=1`, named `argv[0]`, empty `envp`, `AT_PAGESZ`, `AT_ENTRY`, and `AT_NULL`; static libc smoke validates the layout. |
| 5 | Executable lifecycle | A checked limited native `execve` replaces static x86-64 `ET_EXEC` image/stack, activates the new CR3, resets image-local TLS/tid state, and applies `FD_CLOEXEC`. The original image remains intact on preparation failure. |
| 6 | Linux runtime alignment | Linux x86-64 adapter reached L3: L0 I/O/process calls, L1 bounded memory calls, L2 TLS/identity calls, and L3 `AT_FDCWD`-only `openat`, `newfstatat`, and fd-backed `getdents64`. |
| 7 | Evidence and accounting | QEMU requires L0, L1, L3, native exec, static libc, bounded HTTP parser, and package-repository markers. The completed gate passed after the timeout, native runtime and package-fetch additions. |
| 8 | Package retrieval bootstrap | `pkg fetch http://host/path/package.atpk` resolves through configured UserNet, streams a bounded HTTP response through TCP, requires ATPK validation, atomically caches with `O_EXCL`, then invokes the transactional installer. |
| 9 | Native at-style file runtime | Native ABI v1.12/syscall v11 adds `openat` and `fstatat` for **AT_FDCWD only**, with `fstatat` flags `0`/`AT_SYMLINK_NOFOLLOW`; real dirfds fail explicitly. |
| 10 | Bounded socket runtime | Native static libc adds TCP `send`/`recv` with checked CPL3 copies, 1–512 byte limits, and narrow message flags; smoke proves invalid/unconnected paths, not an external exchange. |
| 11 | Repository convenience | `pkg repo show`, `pkg repo set http://host/base`, and `pkg get <name>` persist a sanitized clear-text base and resolve only `<base>/<name>.atpk`; URL/name parser QEMU coverage is deterministic. |

## Current practical profile

The 50% estimate is based on practical source support, not a raw function count. Files, paths, metadata, directory traversal, basic descriptor lifecycle, static process lifecycle, startup vectors, bounded sleep/time, and bounded memory are weighted because they are necessary to execute and test useful static programs. Signals, threads, full terminal control, dynamic linking, full socket readiness and `fork` are not inflated into the estimate.

| Profile family | Current evidence-based position | Reason |
|---|---:|---|
| Files, paths, metadata and directories | High | Broad VFS-backed operations, native directory streams, and bounded Linux directory enumeration are implemented and tested. |
| Descriptors, pipes and readiness | Medium | Core lifecycle and descriptor flags are present. Pipes support finite 10 ms PIT-quantized waits, but socket/VFS readiness and interruption-aware infinite waits are absent. |
| Time and runtime services | Medium | Core clock and sleep APIs work through bounded PIT semantics; advanced timer/timezone services do not. |
| Process startup and lifecycle | Medium | Startup vectors, wait/reap and bounded exec are real; `fork`, signals and process groups remain absent. |
| Memory and Linux ABI | Medium | Native/Linux brk and anonymous mappings plus selected L3 file calls exist within a bounded window. |
| Socket runtime | Low | Checked TCP `send`/`recv` joins the existing socket foundation, but full socket semantics and socket readiness remain absent. |
| Threads, signals and advanced IPC | Low | Intentionally outside this milestone. |

## Current acceptance evidence

The local QEMU harness boots the generated Limine ISO and requires these serial markers.

| Marker | Required meaning |
|---|---|
| `[linux] l0-ok` | Real Linux x86-64 `syscall` L0 entry/dispatch succeeded. |
| `[linux] l1-ok` | Bounded Linux memory probe succeeded. |
| `[linux] l3-ok` | Linux `openat`, `newfstatat`, and `getdents64` probe succeeded. |
| `[exec] ok` | Native static exec replacement and close-on-exec probe succeeded. |
| `[libc] smoke-ok` | Static native libc startup/runtime/descriptor/time smoke, including AT_FDCWD-only file calls, bounded socket `send`/`recv` validation, and finite pipe `poll`/`select`, succeeded. |
| `[http] parser-ok` | Bounded HTTP status/header parser accepted only the supported `200 + Content-Length` framing and rejected unsupported protocol paths deterministically. |
| `[pkg] repo-ok` | Bounded repository base/name parser accepted only safe `http://` paths and one `<base>/<name>.atpk` construction without changing persistent configuration. |

The QEMU gate passed after the finite timeout, native at-style/socket runtime, HTTP parser, and package repository additions. The HTTP check is deterministic parser coverage only: no external repository download is claimed from the default QEMU harness. The code and ISO remain local; no GitHub publication or push is authorized by this roadmap.

## Remaining work after the milestone

The next milestones must be selected for semantic value, not by adding wrappers. The highest-value work is full but bounded `argv`/`envp` marshalling, a reviewed signal/futex/thread design, socket/VFS readiness, stronger dirfd support, and a broader Linux runtime subset only when each syscall has a dedicated CPL3 regression. For package distribution, the next security gate is repository signing plus HTTPS/TLS; the present HTTP bootstrap is intentionally not a trusted-production transport.

| Area | Current boundary | Prerequisite for counting future work |
|---|---|---|
| `execve` arguments and environment | NULL or one `argv[0]`; empty `envp` only | Bounded vector/string copying, rollback tests and repeated CPL3 replacement tests. |
| `fork` | Not implemented | Address-space/object lifetime design, descriptor sharing rules and deterministic reaping. |
| Signals and threads | No POSIX signal or pthread model | Explicit delivery, masking, TLS, wakeup and teardown semantics. |
| Readiness | Pipes have finite PIT-quantized waits only | Event-driven wakeup and safe support for sockets/VFS descriptors. |
| Package transport | Clear-text HTTP with bounded `Content-Length`; no redirects or authentication | Signed repository metadata and a reviewed HTTPS/TLS implementation with real network integration evidence. |
| Native/Linux path calls | `AT_FDCWD` only | Task-owned dirfd resolution with no lexical or mapping escape. |
| Socket runtime | TCP stream calls with 1–512 byte `send`/`recv`; no socket readiness | Connected-path and external network regressions, options, UDP and stream buffering design. |
| Dynamic linking/musl | Not claimed | ELF interpreter, relocations, richer startup auxv, TLS, futex/thread and Linux syscall coverage. |

## Non-claims

Even at this milestone, ATMKoala does not claim POSIX certification, arbitrary Linux/musl binary execution, glibc support, dynamic linking, complete BusyBox support, actual dirfd-relative native path resolution, full socket behavior, full `ioctl`, complete `fcntl`, readiness beyond pipes, interruption-aware infinite `poll`/`select`, full `execve`, `fork`, job control, sessions, signals, `pthread`, futex support, HTTPS/TLS, signed repositories, or secure production-grade package transport.

All future work remains subject to the independent architecture, MIT-compatible code subset, frozen ATMBOOT/ATMUEFI components, provenance requirement for external references, and local-only publication policy.
