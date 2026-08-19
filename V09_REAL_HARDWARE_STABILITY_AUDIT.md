# ATMKoala v0.9 — аудит стабильности на реальном железе

## Наблюдаемая проблема

`hwinfo` and `de` may hang on physical x86-64 machines. QEMU does not reproduce every firmware, EC, VBE and PIT behaviour, so the remediation avoids any repeated or unbounded direct probe in interactive code paths.

| Path | Current behaviour | Real-hardware risk | Remediation |
|---|---|---|---|
| `hwinfo` | Re-runs `cpu_detect(&g_cpu)`. | `cpu_detect` runs PIT-based `cpu_tsc_freq()` and can synchronously read an MSR on J4105 profile. If timer progress is compromised, `pit_sleep` has no timeout. The function named `rdmsr_safe` does not trap #GP. | Print boot-time cached CPU facts; replace timer calibration with CPUID-derived frequency only. |
| `de` launch | Calls `pit_sleep(300)` before Exp starts. | A 3-second wait has no deadline if IRQ0 is unavailable or masked; user sees a shell freeze. | Remove blocking launch delay; draw a single handoff frame instead. |
| Exp tray | Calls `battery_update` every 100 PIT ticks. | Direct EC ports `0x62/0x66` rely on undocumented vendor register offsets. Existing short spin limits prevent an infinite loop but a misbehaving EC can still stall Exp perceptibly and is unsafe to poll repeatedly. | Disable raw EC polling by default; show cached/unavailable telemetry unless an explicit, validated driver is introduced. |
| Exp scale | Starts at 130%; settings and public macros support 80–150%. | Per-frame coordinate transforms, image/layout clipping and persisted state multiply the real-VBE validation surface. | Set compile/runtime scale to 100; delete input, settings and persistence path. |

## Safety rules

No interactive shell command should issue raw MSR, PCI, EC or timer-dependent calibration accesses unless the capability was validated at boot and the operation has a deadline. `hwinfo` must be descriptive, not an active hardware-probing command. Exp must never synchronously wait for PIT time before painting its first responsive frame.

## Scope for this increment

The stabilization makes cache-only CPU reporting the default, disables raw EC telemetry, removes the pre-Exp sleep, fixes Exp at 100%, and retains only bounded CPUID as a dynamic CPU data source. Full ACPI/EC, HPET and power-management support require separate driver work and are explicitly out of scope.

## QEMU regression evidence

The revised `hwinfo` command completed in an Exp terminal and returned to its prompt. It reported cached CPU policy data and explicitly displayed `TSC: unavailable (no calibrated timer probe)` under QEMU, confirming it no longer waits for PIT calibration. The `Alt+F1 → de` path then re-entered Exp and restored the terminal window without the removed 300-tick pre-launch delay. Captures: `/tmp/v09-hwinfo.png` and `/tmp/v09-de.png`.

The rebuilt ATMUEFI ESP was also tested in OVMF through ATM Loader tile 1. The UEFI GOP handoff reached Exp, `hwinfo` completed in the terminal, and the prompt returned with the same non-calibrated TSC status. Capture: `/tmp/v09-uefi.png`.

The same OVMF session completed `posix test` after linking the GUI ABI capability stubs. The terminal reported `paging=OK`, `uaccess=OK`, `vfs-posix=OK`, `syscall-usercopy=OK`, `process-fd=OK`, `native-cpl3=OK` and `static-libc=OK`. Capture: `/tmp/v09-uefi-posix.png`.
