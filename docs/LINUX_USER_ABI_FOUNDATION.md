# Linux x86-64 User ABI Foundation

**Status:** local implementation contract; L0 and bounded L1 have passed local QEMU regression. This is not a Linux-kernel compatibility claim and is not published.

ATMKoala will add a **separate Linux-shaped user ABI layer** beside its native ABI. The goal is to make it possible to evaluate selected **statically linked, x86-64 musl** programs without replacing the ATMKoala kernel ABI, boot flow, desktop, or VFS. musl is designed on top of the Linux syscall API, so compatible function names alone are insufficient; the transition instruction, syscall numbers, structure layouts, process-entry stack and negative-errno behavior must agree.[1] [2]

> The first target is a static, non-PIE `ET_EXEC` program built for Linux x86-64 with a deliberately bounded syscall subset. Dynamic linking, Linux namespaces, Linux device drivers, and full Linux binary compatibility remain out of scope.

## Compatibility boundary

| Layer | First compatibility target | Explicit initial limit |
|---|---|---|
| Instruction ABI | x86-64 `syscall`; number in `RAX`; arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`; result in `RAX` | Existing ATM native `int $0x80` stays supported and unchanged. |
| Error ABI | Kernel returns `-errno` in `RAX`; musl converts `-1..-4095` to `errno` | No fabricated success or unsupported-call aliases. |
| Syscall numbers | Linux x86-64 numbers from the upstream syscall table | Only listed milestone calls are dispatched; all other numbers return `-ENOSYS`. |
| ELF image | ELF64 little-endian x86-64 `ET_EXEC`, `PT_LOAD`, BSS, flags and entry validation | No `ET_DYN`, interpreter (`PT_INTERP`), relocation or ELF dynamic loader in the first milestone. |
| Initial stack | 16-byte-aligned `argc`, `argv[]`, `envp[]`, `auxv[]` with terminator | Minimal deterministic arguments/environment; no vDSO or `AT_SYSINFO_EHDR`. |
| TLS | `arch_prctl(ARCH_SET_FS/ARCH_GET_FS)` needed before musl thread-local runtime work | pthread, clone, futex and signal-restorer semantics are a later milestone. |
| File/process API | Linux-numbered adapters over verified task-owned descriptors, paths, pipes, directory streams and wait lifecycle | Existing VFS semantics remain bounded; Linux mount/device/ioctl coverage is not assumed. |

## Milestone syscall contract

The following calls are ordered by a static-musl bootstrap dependency, not by an assertion that every syscall is already available. Numbers are from Linux's official x86-64 syscall table.[3]

| Milestone | Linux syscall numbers | ATMKoala implementation commitment |
|---|---|---|
| L0: proof of entry | `write=1`, `exit=60`, `exit_group=231`, `getpid=39` | **Implemented and QEMU-probed** with an original static ELF that executes the actual `syscall` instruction. |
| L1: static runtime memory | `brk=12`, `mmap=9`, `munmap=11`, `mprotect=10` | **Implemented and QEMU-probed.** `mmap` accepts only non-fixed, anonymous, private mappings with `fd=-1`, uses top-down placement in a bounded 112 MiB anonymous arena, and rejects `PROT_NONE` and file-backed/shared mappings. `munmap` and `mprotect` operate only in that arena. |
| L2: C runtime identity/TLS | `arch_prctl=158`, `set_tid_address=218`, `gettid=186`, `uname=63` | Implemented with checked user-copy and per-task FS-base restoration. `set_tid_address` clears the word at task exit but has no futex wake. A dedicated static L2 end-to-end probe remains required before treating this row as validated. |
| L3: file and path subset | `read=0`, `write=1`, `open=2`, `close=3`, `lseek=8`, `fstat=5`, `newfstatat=262`, `getdents64=217`, `openat=257`, `readlink=89` | Translate Linux layouts and flags only where ATMKoala semantics are implemented; unsupported flags return `-EINVAL` or `-ENOSYS`. |
| L4: process/IPC subset | `pipe=22`, `poll=7`, `wait4=61`, `kill=62`, `dup=32`, `dup2=33`, `fcntl=72` | Adapt verified pipes, wait, non-blocking flags and zero-timeout readiness. Blocking readiness is deferred until independently tested. |
| L5: time and signals | `clock_gettime=228`, `nanosleep=35`, `rt_sigaction=13`, `rt_sigprocmask=14` | Required for broad musl execution; not included before safe signal-frame and scheduler work exists. |

## Implemented address-space boundary

Each native user task receives a 128 MiB region from `0x40000000` to `0x48000000`. Static ELF segments and `brk` are confined below `0x41000000`; anonymous L1 mappings occupy the remaining bounded arena beneath the dedicated top stack page. Mapping metadata is heap-owned rather than embedded in `user_space_t`, preventing 4 KiB static-ELF probe frames from overflowing the existing task stack.

## Required process-entry records

The current initial stack is a 16-byte-aligned minimal frame containing `argc=0`, null `argv`, null `envp`, and an auxiliary-vector terminator. It is adequate for the hand-built ABI probes only. Before musl startup is claimed, it must be replaced by a documented System V frame with real `argc`/`argv`, empty environment, and at least `AT_PAGESZ`, `AT_ENTRY`, `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_UID`, `AT_EUID`, `AT_GID`, `AT_EGID`, `AT_SECURE`, and `AT_NULL`.[4]

`AT_RANDOM`, a genuine per-exec random payload, must be provided before claiming stack-protector-ready musl startup. `AT_SYSINFO_EHDR` is omitted until ATMKoala has a separately reviewed vDSO implementation; the Linux x86 auxiliary-vector documentation identifies it as the address of the vDSO page.[4]

## Provenance and implementation rule

No musl source is imported by this design document. The musl reference record is located at `docs/provenance/musl-linux-abi-reference.md`. Every compatibility adapter is to be written originally from public ABI specifications. If any musl file, header, crt object or assembly is later imported, its exact revision, path, notice, local changes and tests must be recorded before import, as required by `DEVELOPMENT_POLICY.md`.

## Non-claims

ATMKoala does **not** currently claim support for arbitrary Linux binaries, musl binaries, dynamically linked ELF programs, PIE executables, pthread programs, futex wake semantics, file-backed mappings, Linux signals, vDSO, or a full Linux process-entry stack. Only the original L0 and bounded L1 hand-built static ELF probes have passed QEMU regression. A binary is called supported only after it loads and runs through an equivalent QEMU regression using the Linux-style ABI.

## References

[1]: https://musl.libc.org/about.html "About musl"
[2]: https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT "musl COPYRIGHT and MIT license"
[3]: https://raw.githubusercontent.com/torvalds/linux/master/arch/x86/entry/syscalls/syscall_64.tbl "Linux x86-64 syscall table"
[4]: https://docs.kernel.org/arch/x86/elf_auxvec.html "Linux x86 ELF auxiliary vectors"
[5]: https://man7.org/linux/man-pages/man2/syscall.2.html "syscall(2): architecture calling conventions"
