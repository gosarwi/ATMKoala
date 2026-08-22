# ATMKoala Exp Productivity & Storage Batch — Validation Record

**Scope.** This record covers the active Limine hybrid ISO source tree only. It records the local verification of the bounded Exp additions completed on 2026-08-22. It does not represent a GitHub release or publication.

## Implemented scope

| Area | Verified implementation | Deliberate boundary |
|---|---|---|
| Viewer | Decodes bounded binary PPM P6: comments before header fields, `maxval=255`, existing dimension/pixel caps and RGB payload validation. | No PPM P3, 16-bit PPM or image encoder was added. |
| Files | Current-directory 31-byte literal filename find (`F`) and a read-only properties pane (`I`). | No recursive indexing, directory copy, rename workflow or unknown-origin Trash restore. |
| Notepad / Journal | `Ctrl+F` literal find, `Ctrl+G` positive line navigation and an explicit `Enter` discard / `Esc` cancel close decision for modified text. | No regex, replace-all, recovery journal or external editor ABI. |
| Package Details | Optional persisted ATPK description is shown for the visible registry row. | No dependency solver, background update, TLS transport or GUI install/remove path. |
| Diagnostics | `E` explicitly exports a collision-safe CatFS status report; `R` retains local parser/input checks. | Export is a requested CatFS write, not a hidden test; it performs no network, RTC, mount or disk action. |
| Storage | Read-only MBR view of an already detected ATA drive and bounded ext2/CatFS signature probes. | No partition editor, repair routine, arbitrary hardware probe or mount selection. |
| Help | Four static Help Center pages and 30-entry responsive launcher. | No online documentation updater or background assistance service. |

## Build and regression results

| Command | Runs | Result |
|---|---:|---|
| `make all` | Repeated during implementation and immediately before ISO creation | Passed |
| `make atmloader` | 1 | Passed; active Limine hybrid ISO produced |
| `bash tests_qemu_linux_l0.sh` | 2 | Passed both times |
| `bash tests_qemu_ext2_write_type.sh` | 2 | Passed both times; guarded direct-block persistence confirmed |

> The primary QEMU harness covers the aggregate Exp selftest, including the new PPM format-routing fixture and pure filename-search/package-accessor invariants. It does not visually certify every interactive Exp workflow.

## Final local artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `build/kernel.bin` | 764,904 bytes | `c29684547a9a1be3f0354a9fdb626225779e3d7b75989ed17d015a456b13e175` |
| `atmkoala-OS-v0.9-limine.iso` | 6,154,240 bytes | `f3039252844eff0751f36b8390e2e28e7a1dd0fd552c67665032a8a97a20ef75` |

## Publication status

No source, ISO, GitHub repository or release page was published as part of this batch. Publication remains a separate action that requires a fresh explicit user request.

## Related status document

The authoritative feature and compatibility description is [`POSIX_COMPATIBILITY_STATUS.md`](POSIX_COMPATIBILITY_STATUS.md). Its externally referenced time-format and NTP material is cited there.[1] [2] [3]

## References

[1] [IANA, *Time Zones*](https://www.iana.org/time-zones)

[2] [IETF, *RFC 9636 — The Time Zone Information Format (TZif)*](https://datatracker.ietf.org/doc/rfc9636/)

[3] [IETF, *RFC 5905 — Network Time Protocol Version 4*](https://datatracker.ietf.org/doc/html/rfc5905)
