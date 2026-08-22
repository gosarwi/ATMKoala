# ATMKoala Disk Installer Final Activation — Validation Record

**Scope.** This record covers the active Limine hybrid ISO source tree only. It documents the final-installer activation repair completed on 2026-08-22. No GitHub publication occurred.

## Problem addressed

The prior installer selftest verified only bounded UI geometry and parser-like confirmation state. It did not traverse the full graphical installer or invoke the final destructive action. Therefore it could not prove that typing the confirmation and pressing Enter reached the real MBR/CatFS installation transaction.

## Repair

| Area | Implemented behavior |
|---|---|
| Final action | After exact case-insensitive `ERASE`, **Enter**, keypad Enter, **Space**, or a click in the same enabled Install rectangle activates the transaction. Activation is checked before text ingestion, so Enter cannot clear the unlock state. |
| Feedback | The final page reports successful unlock with `ERASE accepted. Press Enter, Space, or click Install.` A UTF-8/Russian-layout key produces a specific instruction to type `ERASE` in English. |
| Observability | The installer emits bounded serial markers for ready, each slide, confirmation unlock, destructive activation and transaction result. |
| Regression | `tests_qemu_installer_commit.sh` creates a brand-new 64 MiB `/tmp` image, boots the dedicated installer, completes all slides, types a valid root password and `erase`, activates with **Enter**, requires transaction markers, and checks MBR signature `55 aa`. It never references host or user disks. |

> The installer remains deliberately destructive only after explicit typed confirmation. This regression validates its behavior on a disposable QEMU disk image; it is not a certification for every physical ATA, firmware, storage controller, or power-loss scenario.

## Verification results

| Command | Runs | Result |
|---|---:|---|
| `make all` and `make atmloader` | Final rebuild | Passed |
| `bash tests_qemu_installer_commit.sh` | 2 | Passed twice; real transaction accepted **Enter** after `erase` and wrote the disposable MBR |
| `bash tests_qemu_linux_l0.sh` | 2 | Passed twice; includes pure installer UI selftest |
| `bash tests_qemu_ext2_write_type.sh` | 2 | Passed twice; guarded Ext2 direct-write persistence unchanged |

## Final artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `build/kernel.bin` | 765,232 bytes | `1134532448329b7fa036781c3867c0ad2735edf3fb922598a8d109801fe88770` |
| `atmkoala-OS-v0.9-limine.iso` | 6,154,240 bytes | `164e8aa60ebccbaa95d7a0c592d07ab6c27ed9d570ecefee46b3f05699235356` |

## Publication status

The source and ISO remain local. Publishing is a separate action and requires a fresh explicit request.
