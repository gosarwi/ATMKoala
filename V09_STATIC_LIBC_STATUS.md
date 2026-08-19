# ATMKoala v0.9 — static native libc status

## Implemented baseline

The project now contains an independent, MIT-native static libc skeleton under `sdk/libc/`. It includes:

| Component | Status |
|---|---|
| `crt0.s` | `_start` receives the loader RSP, retains System V call alignment and enters C startup. |
| `crt.c` | Decodes v0.9 loader words, calls `main(argc, argv, envp)` and terminates via ATM Native ABI. |
| `string.c` | Independent `mem*`, `str*`, `strlen` and bounded variants; no kernel helper or glibc dependency. |
| `atm_native.ld` | Static x86-64 `ET_EXEC` linker script at `0x40000000`. |
| Build fixture | `libc_smoke.elf` is built with `-nostdlib`, embedded in the kernel, loaded through the real ELF loader and run in CPL 3. |

## Critical correction discovered by C code

The prior generated native probe did not use its user stack. The first compiled C application executed a `call`, which pushed a return address to the NX-marked user stack and exposed an EFER configuration defect: ATM page tables emitted the NX bit but kernel entry did not enable `EFER.NXE`. This caused a user page fault at `0x401fffd8` with error code `0xE`.

Both BIOS long-mode setup and the UEFI entry now enable `EFER.NXE`. The active stack PTE is thereby interpreted as non-executable rather than reserved, and ordinary C calls can use it safely. The minimal application `main() { return 42; }` now passes the complete static chain:

> static ELF64 → loader `PT_LOAD` mapping → CPL 3 `_start` → C CRT → `main` → `exit(42)` → parent `waitpid`.

QEMU reports `static-libc=OK` together with all existing paging, user-copy, process-FD and native-CPL3 checks.

## Next slice

The next increment adds public `errno`, POSIX-style wrappers that translate negative raw results to `-1`, and a simple `brk`-backed malloc/free allocator. Then the smoke application will re-enable string, heap and write calls as a real C runtime proof.

## Expanded runtime regression

The smoke application now exercises `calloc`, `strcpy`, `strlen`, `strcmp`, `realloc`, `free` and POSIX-style `write` before exiting with status 42. Its full QEMU lifecycle passes after three foundation corrections discovered by realistic C code:

| Finding | Correction |
|---|---|
| The initial user stack was marked NX while EFER.NXE was disabled. | Enable NXE in both BIOS long-mode bootstrap and UEFI direct entry. |
| Kernel fd 0–2 were `/dev/null`, so native `write(1,...)` could not implement usable stdio semantics. | Give each native task separate `/dev/tty` opens and release them at task exit. |
| A direct CPL3 exit could leave a NULL-linked zombie as run-queue head; the next native task made `sched_tick` spin. | Treat a NULL-linked head removal as an empty queue. |

The resulting terminal regression returns `paging=OK`, `uaccess=OK`, `vfs-posix=OK`, `syscall-usercopy=OK`, `process-fd=OK`, `native-cpl3=OK`, and `static-libc=OK`. The native app’s `write` return is verified as successful by the C smoke process; rendering of `/dev/tty` is a separate terminal-routing concern and is not yet a GUI-application output ABI.
