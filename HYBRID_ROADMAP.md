# ATMKoala Independent Hybrid Roadmap

**Status:** local planning document. No GitHub publication is authorized.

## Milestone H1 — Hardware Transport Foundations

The first hardware milestone is an original xHCI-oriented transport layer. It starts with PCI BAR validation and controller reset, then adds DMA-safe ring allocation, command completion accounting and bounded error recovery. Only after that can USB descriptor enumeration and HID boot-protocol mouse reports be implemented. A completed milestone requires a QEMU xHCI test with a USB tablet or mouse plus a real-hardware `mouse` diagnostic that reports controller state, enumerated device and received report count.

USB mass storage is intentionally separate. It depends on the host-controller layer, then requires BOT transport and minimal SCSI capacity/read handling before a read-only block adapter may be exposed. Partition probing and writes remain disabled until the adapter passes corruption and unplug/error-path tests.

## Milestone S1 — Storage Controller Reachability

PCI bridge recursion now makes AHCI and NVMe controllers visible in `lsblk` diagnostics. The next implementation order is AHCI read-only identify/read, then NVMe identify/read, then per-controller error and timeout reporting. No controller becomes an install target until writes, flush semantics and partition-boundary checks are tested.

## Milestone P1 — POSIX Process and IPC Foundation

The next userspace work adds original task creation, parent/child ownership, exit status and bounded wait semantics. Pipes follow only after descriptor lifetime and blocking/wakeup behavior are specified. Directory streams and signal-like termination behavior are subsequent pieces. Every API must be exposed through the native ABI, libc subset, a CPL3 smoke fixture and a QEMU regression before it is described as supported.

## Test Gates

| Gate | Required evidence |
|---|---|
| Build | `make all` completes with no errors or warnings introduced by the slice. |
| Firmware | Hybrid ISO boots via BIOS QEMU and OVMF/UEFI QEMU. |
| Safety | Invalid PCI BAR, timeout, malformed descriptor and invalid user pointer paths return without a kernel hang or write. |
| Functional | A focused regression demonstrates real data flow, not only device/controller detection. |
| Documentation | Command output and local documentation state remaining limits precisely. |

## Immediate Work Order

1. Define xHCI DMA/ring memory boundary and interrupt/polling model.
2. Keep current PS/2 mouse as fallback, but do not mislabel it USB support.
3. Keep PCI bridge discovery and controller diagnostics as inventory-only until an original data path exists.
4. Continue POSIX process/IPC planning independently of USB progress.
