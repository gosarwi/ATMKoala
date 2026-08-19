# ATMKoala v0.9 — native libc ABI readiness audit

**Conclusion:** текущий v0.9 ABI достаточно зрел для первой **static single-threaded libc** и одного C application process. Он пока недостаточен для прямого полного musl port, threads, dynamic loading или applications expecting Linux process startup.

## Verified runtime contract

| Contract | Current state | libc consequence |
|---|---|---|
| Architecture | x86-64 System V C calling convention inside app. | C compiler can generate ordinary freestanding code. |
| Entry image | ELF64 `ET_EXEC`, `PT_LOAD`, fixed user address window. | Linker script must output static non-PIE executable. |
| User transfer | Loader enters CPL 3 with 16-byte aligned stack. | Small CRT assembly may call C bootstrap safely. |
| Startup words | `argc=0`, `argv=NULL`, `envp=NULL`, auxv terminator. | `main(int,char**,char**)` can be called now; argv/envp population is later `execve` work. |
| Syscall ABI | `int $0x80`: `RAX` number, `RDI/RSI/RDX` first 3 args; negative errno-style result. | libc wrappers can translate failure to `errno` and `-1`. |
| Process | Static app task; `exit`, `waitpid`, IDs. | `exit` and basic process identity ready. |
| Descriptors | Task-local namespace 0–31; fd 0–2 attached to console backend. | `open/read/write/close/lseek/fstat` wrappers are viable. |
| Heap | `brk(0)` and bounded private heap grow. | Simple malloc allocator is viable. |
| Scheduler | Entry/exit lifecycle verified. | No safe user timer-preemption, TLS, signals or pthreads yet. |

## Immediate libc implementation profile

The first runtime should be independent source under `sdk/libc/`, built into each app with `-ffreestanding -nostdlib`. It needs four layers:

1. **CRT:** `_start` forwards the loader stack to `__atm_libc_start_main`, invokes `main(0,NULL,NULL)`, then calls `atm_exit`.
2. **Syscall facade:** raw wrappers stay private; public POSIX-style calls return `-1` and set native `errno`.
3. **Pure C core:** `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strcpy`, basic integer conversion and allocator metadata.
4. **I/O:** unbuffered `write`-first helpers; `stdio` waits until a tested `FILE` design exists.

## Required corrections before public release

- Replace outdated internal header comment that calls the gate “ring-0 only”; QEMU now verifies real CPL 3 invocation.
- Add all public error numbers that wrappers can expose; a libc must never collapse a native error into an undocumented value.
- Keep current ABI calls to three arguments in the first libc. `R10/R8/R9` can be introduced after a six-argument assembly wrapper and a real user caller test.
- Include a standalone linker script and one C application test; generated machine-code probe does not validate a compiled CRT.

## Explicitly deferred

`fork`, `execve`, `pipe`, `poll`, `mmap`, `munmap`, `ioctl`, `clock_gettime`, signals, `clone`, `futex`, TLS, `pthread`, locale catalogs, resolver, `dlopen` and `PT_INTERP` are not requirements for this increment. Any third-party musl source depending on them stays excluded until its underlying ATM ABI surface exists.
