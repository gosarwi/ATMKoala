# ATMKoala POSIX Compatibility Status

**Status:** Local implementation record for ATMKoala v0.9, **native ABI v1.12**, **native syscall ABI v11**, and the bounded **Linux x86-64 ABI L3** adapter. The practical source-profile estimate is now **approximately 50%** under the project’s documented, weighted profile method. This is **not** POSIX certification, complete Linux ABI compatibility, BusyBox/Toybox compatibility, musl readiness, or a claim that arbitrary binaries execute.

> A facility is counted only when it has defined bounded semantics, checked CPL3 memory handling where applicable, an implementation in the freestanding runtime or kernel, and a real CPL3/QEMU regression. Headers, stubs, and unconditional-success paths contribute no profile coverage.

## Scope of the estimate

The estimate covers the practical static-application profile that is relevant to ATMKoala today: files and namespace operations, descriptor lifecycle, directories, bounded process lifecycle, startup/runtime, time queries and sleep, memory basics, and a small Linux-oriented runtime adapter. It deliberately assigns little or no weight to still-absent `fork`, signals, threads, dynamic linking, terminal control, full sockets, and advanced asynchronous I/O.

| Profile family | Verified position | What is counted | Important boundary |
|---|---:|---|---|
| Files, paths, metadata and directories | High | Read/write, positional I/O, vector I/O, selected AT_FDCWD-only metadata/path APIs, links, relative paths and directory traversal | Semantics remain bounded by the ATMKoala VFS and mounted filesystem support; native dirfd-relative resolution is not implemented. |
| Descriptors, pipes and readiness | Medium | `dup`, `dup2`, `F_DUPFD`, status and descriptor flags, pipes, nonblocking pipe reads, bounded finite `poll` and `select` waits | No socket/VFS polling, `epoll`, `ioctl`, descriptor passing, or infinite interruption-aware waits. |
| Time and runtime services | Medium | Monotonic/realtime clocks, resolution query, wall clock, `nanosleep`, `sleep`, `usleep`, and bounded local civil-time display | PIT quantization is 10 ms; no timerfd, alarms, NTP, full TZif history, or automatic timezone-rule updates. |
| Process startup and lifecycle | Medium | Static process startup vector, basic process identity, `waitpid`, limited `execve`, close-on-exec cleanup | No `fork`, process groups, sessions, full signals, job control, threads, or dynamic linker. |
| Memory and Linux runtime ABI | Medium | Native/Linux-shaped `brk`, bounded anonymous `mmap`, `mprotect`, `munmap`, TLS base calls, selected file/path calls | No file-backed/shared mappings, demand paging, ELF interpreter, or general Linux runtime support. |
| Socket runtime | Low | TCP `socket`, `connect`, `bind`, `listen`, `accept`, bounded `send`, and bounded `recv` | Not counted as a full socket API; no UDP, resolver API, socket readiness, options, scatter/gather, or external TCP QEMU exchange. |
| Signals, threads and advanced IPC | Low | Cooperative `kill` foundation and pipes only | Not counted as substantially complete. |

## Verified native source profile

| Area | Public interfaces and semantics | Evidence | Deliberate boundary |
|---|---|---|---|
| File descriptors and paths | `open`, `openat(AT_FDCWD)`, `close`, `read`, `write`, `pread`, `pwrite`, `lseek`, `dup`, `dup2`, `readv`, `writev`, `fsync`, `fdatasync`, `truncate`, `ftruncate` | Static CPL3 libc smoke and kernel selftests | `openat` rejects all real dirfds; no `ioctl`, advisory locking, descriptor passing, or asynchronous file I/O. |
| Metadata and namespace | `stat`, `lstat`, `fstat`, `fstatat(AT_FDCWD)`, `chmod`, `chown`, `mkdir`, `rmdir`, `unlink`, `rename`, `link`, `symlink`, `readlink`, `access`, `chdir`, `getcwd`, `umask` | Static CPL3 libc smoke and kernel POSIX selftest | `fstatat` permits flags `0` or `AT_SYMLINK_NOFOLLOW` and rejects real dirfds; permission and backing-store behavior remain VFS-limited. |
| Directory streams | Native `opendir`, `readdir`, `closedir`; Linux ABI `getdents64` over an open directory descriptor | Static libc smoke and Linux L3 CPL3 probe | Native handles and Linux descriptor streams are bounded; no `telldir`, `seekdir`, or arbitrary dirfd-relative resolution. |
| Descriptor status and lifecycle | `fcntl(F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL)`, `FD_CLOEXEC`, `O_CLOEXEC`, access-mode preserving `F_GETFL`, mutable `O_NONBLOCK` | Kernel descriptor selftest, static libc smoke, and exec regression | Only the listed `fcntl` commands exist. `O_NONBLOCK` affects the supported pipe path; this is not a complete file/socket flag model. |
| Pipes and readiness | `pipe`, pipe EOF/HUP behavior, `O_NONBLOCK`, `poll`, `select` | Static libc smoke and dedicated pipe IPC regression | Readiness remains pipes only, with at most 16 watched descriptors. Finite waits up to 600,000 ms are rechecked each 10 ms PIT tick; `poll(-1)` and `select(..., NULL)` remain unsupported because interruption semantics do not exist. |
| Process basics | `getpid`, `getppid`, `getuid`, `getgid`, `gettid`, `waitpid`, cooperative `kill`, `_exit` | Kernel lifecycle and CPL3 wait regressions | No POSIX signal delivery model, groups, sessions, credentials changes, or `fork`. |
| Static process entry | Initial RSP contains `argc`, a NULL-terminated `argv`, a NULL-terminated `envp`, then auxv pairs. Current launcher supplies `argc=1`, named `argv[0]`, empty `envp`, `AT_PAGESZ`, `AT_ENTRY`, and `AT_NULL`. | Static CPL3 libc smoke validates vectors and auxv | General caller-selected argument/environment marshalling is reserved for a later expansion. |
| Limited executable replacement | `execve(path, argv, envp)` replaces the current native static `ET_EXEC` image only after path/image/stack preparation succeeds; `FD_CLOEXEC` descriptors close on success and old image mappings are released after CR3 transition | Dedicated CPL3 exec probe | Only static x86-64 `ET_EXEC`; `argv` is NULL or a single `argv[0]`; `envp` is NULL or empty; no `fork`, interpreter, dynamic linker, or multithreaded exec semantics. |
| Socket runtime | TCP `socket`, `connect`, `bind`, `listen`, `accept`, `send`, `recv` | Static libc smoke executes checked unconnected and invalid-flag `send`/`recv` paths | `send` accepts one 1–512 byte segment and flags `0`; `recv` accepts 1–512 bytes, flags `0` or `MSG_DONTWAIT`, and uses a bounded 3 s default wait. No UDP or real external exchange is claimed. |
| System identity | `uname` through `sys/utsname.h` | Static CPL3 libc smoke | Fixed local identity strings only; no mutable hostname/domain API. |
| Time | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `clock_getres`, `gettimeofday`, `time`, `nanosleep`, `sleep`, `usleep` | Static CPL3 libc smoke and repeated QEMU runs | 100 Hz PIT means successful sleeps are rounded upward to 10 ms ticks; no interruption/remainder semantics beyond the documented bounded path. |
| Memory | Native `brk`; Linux-shaped bounded anonymous `brk`, `mmap`, `mprotect`, and `munmap` | Linux L1 CPL3 probe | The user window is bounded to 128 MiB. No shared/file-backed mappings or demand paging. |

## ABI versions and Linux adapter

The native public header reports **ABI major 1, minor 12**. `ATM_SYS_ABI_INFO` reports **syscall ABI v11**. The static libc smoke verifies that the public ABI probe returns v11, preventing a stale runtime/kernel version report.

The Linux entry remains a separate x86-64 `syscall` path. It is an adaptation layer for carefully selected static-runtime operations, not an alternative kernel personality.

| Linux level | Verified calls | Boundary |
|---|---|---|
| L0 | `read`, `write`, `open`, `close`, `getpid`, `exit`, `exit_group` | Direct numeric compatibility only for the verified subset. |
| L1 | `brk`, anonymous private `mmap`, `mprotect`, `munmap` | 128 MiB bounded user window; no shared/file-backed mapping. |
| L2 | `arch_prctl` for FS base, `set_tid_address`, `gettid`, `uname` | TLS and identity foundation only; no full thread implementation. |
| L3 | `openat`, `newfstatat`, `getdents64` | `openat` and `newfstatat` accept **only `AT_FDCWD`**. `newfstatat` accepts flags `0` or `AT_SYMLINK_NOFOLLOW`. `getdents64` requires an owned directory fd and a caller buffer of at least 280 bytes. |

Linux L3 translates only the explicitly accepted flags. Unsupported flags and non-`AT_FDCWD` dirfds fail rather than being guessed or silently redirected. `getdents64` uses an fd-backed VFS iterator whose offset advances after each emitted entry.

## Regression evidence

The local `tests_qemu_linux_l0.sh` harness boots the locally generated Limine ISO, runs `posix test`, and requires every marker below. The gate has been extended after the package-download work; its deterministic protocol parser marker is required alongside the existing CPL3 checks.

| Required serial marker | What the real CPL3 path proves |
|---|---|
| `[linux] l0-ok` | Linux x86-64 `syscall` entry and L0 dispatch. |
| `[linux] l1-ok` | Bounded Linux-style `brk`, anonymous `mmap`, `mprotect`, and `munmap`. |
| `[linux] l3-ok` | Linux `openat(AT_FDCWD)`, `newfstatat`, and fd-backed `getdents64`. |
| `[exec] ok` | Native static `execve` replaces image/stack and closes an `FD_CLOEXEC` descriptor before the replacement image runs. |
| `[libc] smoke-ok` | Static native libc startup vector, ABI version, AT_FDCWD-only `openat`/`fstatat`, descriptor flags, bounded socket `send`/`recv` validation, finite pipe `poll`/`select` waits, vector I/O, metadata, directories, time/sleep, and identity functions. |
| `[http] parser-ok` | Bounded HTTP parser accepts a valid `HTTP/1.x 200` response with one `Content-Length` and rejects malformed URLs, `https://`, missing lengths, and chunked transfer encoding. This is deterministic parser evidence, not a claim that an external repository was contacted during QEMU. |
| `[pkg] repo-ok` | Repository URL/name parser accepts bounded clear-text `http://` bases and resolves a safe native package name to `<base>/<name>.atpk`; it rejects unsafe paths and unsupported schemes without mutating configuration or contacting a repository. |
| `[installer] ui-ok` | Pure Disk Install hitbox and partition-strip geometry check. It neither probes nor writes MBR, CatFS or any target drive. |
| `[exp] utf8-layout-ok` | Exp verifies that Russian text is measured as seven glyphs for `Русский`, not fourteen UTF-8 bytes; this protects codepoint-aware tab placement and bounded title/text paths. |
| `[time] timezone-ok` | Pure civil-time checks cover `Asia/Yekaterinburg` (UTC+05:00), North American summer/winter offsets, the EU DST boundary, Australian end-of-season transition, Egyptian DST end and rejection of an unknown identifier. |

## Installer UI, timezone and Exp text boundaries

The GUI Disk Install path now draws its cursor above every panel, presents compact disk glyphs in drive selection, and visualizes the proposed partition span before the destructive confirmation step. Its new regression is deliberately pure UI/layout logic; it does not mount, format, inspect or write a disk. Actual installation remains gated by the existing explicit destructive confirmation and retains the preflight path.

The installer timezone selector includes `Asia/Yekaterinburg`, `Asia/Omsk`, `Asia/Novosibirsk`, `Asia/Krasnoyarsk`, `Asia/Irkutsk`, `Asia/Yakutsk`, `Asia/Vladivostok`, `Asia/Magadan`, and `Asia/Kamchatka`; the shell `timezone list` advertises the same embedded-rule identifiers. `date`, Exp Clock and Calendar now convert an RTC configured as UTC to a selected local civil time. The built-in current-era table contains fixed UTC offsets and explicitly coded DST rules for the supported EU, North American, Australian, New Zealand, Chilean, Egyptian and Israeli paths. The default is an **UTC RTC**; `timezone clock local` disables conversion only for firmware deliberately configured to local time.

> The embedded table is not a complete IANA TZif history and it does not receive political time-rule updates automatically. NTP synchronization is still absent. IANA states that the TZ Database is periodically updated when authorities change boundaries, UTC offsets and DST rules; therefore an ATMKoala update is required if a supported region changes its civil-time law.[1]

Exp Clock now presents the selected local date and time, offset/DST state or explicit RTC error, plus a per-window stopwatch. The stopwatch has keyboard `S`/Space to start or pause, `R` to reset and equivalent mouse buttons. Its elapsed time comes from monotonic 100 Hz PIT ticks, so it continues independently of CMOS RTC availability, timezone selection or UTC/local basis changes.

Exp now has bounded UTF-8 rendering helpers. Window titles clip before window controls on codepoint boundaries; localized Settings tabs use codepoint metrics; and localized prose wraps within the content pane. This prevents Russian text from crossing the right edge or being split inside a UTF-8 sequence. It is a UI-layout correction, not a new text-shaping, variable-width-font, or full internationalization engine.

## Package download path

The package shell provides direct `pkg fetch http://host/path/package.atpk` plus repository convenience commands: `pkg repo show`, `pkg repo set http://host/base`, and `pkg get <native-package-name>`. The repository base is persisted in `/uiu/etc/packages.conf`; `pkg get` deterministically resolves the package URL as `<base>/<name>.atpk`. The command requires UserNet to be configured and up, resolves a hostname through the existing bounded DNS path (or accepts dotted IPv4), downloads through the existing TCP client, accepts only HTTP status `200` with one bounded `Content-Length`, and streams the body in at most 512-byte TCP chunks.

Before installation, the response must parse as a native ATPK archive with `ATPK/control` and `ATPK/manifest`; legacy `.tar.zst` is intentionally refused for remote intake. A validated body is written with `O_EXCL` to `/uiu/cache/packages/<name>.atpk.download`, renamed to `/uiu/cache/packages/<name>.atpk`, and only then passed to the existing transactional installer. Existing cache objects are not overwritten implicitly.

> This is a deliberately limited **clear-text HTTP** bootstrap path. It rejects `https://`, redirects, authentication, chunked transfer encoding, proxy configuration, persistent connections, and oversized bodies. There is no TLS authenticity or transport confidentiality claim. Production repository trust requires a future signature and HTTPS/TLS design.

The final local artifact is rebuilt and QEMU-tested locally only. No kernel source or ISO publication, repository publication, or GitHub operation is part of this milestone.

## References

[1] [IANA, *Time Zones*](https://www.iana.org/time-zones)

## Explicit non-claims and next work

ATMKoala does **not** claim POSIX certification, arbitrary Linux binary execution, musl portability, glibc support, dynamic linking, complete BusyBox/Toybox support, a full socket API, full signal semantics, `pthread`, futexes, full `execve` vectors, or `fork`.

The highest-value next work is to extend argument/environment marshalling without weakening user-pointer validation, design a real signal/futex/thread model, add properly timed readiness waits for supported descriptor classes, and expand Linux runtime calls only with corresponding CPL3/QEMU tests. Any such work must retain the bounded address-space, independent-code, and no-unverified-feature rules recorded here.
