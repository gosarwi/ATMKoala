# ATMKoala Independent Hybrid Development Policy

**Status:** local working-tree policy. This file is not published or committed unless the project owner explicitly authorizes publication.

## Direction

ATMKoala remains an **independent freestanding x86-64 operating system**. Its kernel ABI, Exp desktop, boot flow, build system, native libc subset, drivers and application model remain ATMKoala-owned design work. The project does not become a 4.4BSD fork, does not claim BSD binary compatibility and does not import an operating-system base wholesale.

The project may study established BSD interfaces, algorithms and engineering practices as technical references. Such study must lead either to an independently written implementation based on public specifications or to a narrowly scoped, explicitly tracked port whose license has been reviewed as compatible with the project.

## Source and License Rules

| Rule | Required practice |
|---|---|
| Original implementation | Prefer a new ATMKoala implementation from public protocol, ABI or hardware specifications. Keep the result MIT-compatible where possible. |
| Selective external port | Before importing code, record source URL or upstream revision, original path, license text, author/copyright notice, reason for use, local changes and tests. Preserve all required notices. |
| Ambiguous provenance | Do not import it. Reimplement from a public specification or leave the capability unavailable. |
| API compatibility | A POSIX- or BSD-shaped API may be exposed only after its local semantics, error behavior and tests are defined. API naming alone is never a conformance claim. |
| Hardware support | PCI discovery, controller detection and a boot-provided framebuffer are diagnostics, not drivers. A device is called supported only after initialization, data transport and regression coverage are present. |

## Component Record Template

Create one record under `docs/provenance/` before any external source import:

```text
Component:
Purpose:
Source/revision:
Original path:
License and notice:
Why independent implementation is insufficient:
Local changes:
ABI and security review:
Build and QEMU/hardware tests:
Maintainer decision:
```

## Current Technical Priorities

1. **USB xHCI/EHCI transport foundations:** controller reset, DMA-safe allocation, command/event ring handling, descriptor enumeration and HID boot-mouse reports. USB mouse support must not be announced before this chain works.
2. **USB mass storage:** BOT, SCSI READ CAPACITY/READ/WRITE and a bounded block-device adapter, with no automatic write enablement before media and partition validation.
3. **Storage controllers:** original AHCI and NVMe read paths after PCI discovery, then carefully scoped write support.
4. **POSIX userspace:** task/process creation boundaries, wait/exit semantics, pipes, directory streams, signals and libc wrappers, each backed by CPL3 and QEMU tests.
5. **Desktop reliability:** preserve Exp's non-blocking event loop, input diagnostics and bounded native application state.

## Non-claims

ATMKoala currently does **not** claim complete POSIX, BSD or Linux ABI compatibility. It does not have USB HID transport, USB mass storage, AHCI/NVMe block drivers, a dynamic linker, glibc, hardware OpenGL, Mesa/GLX, or a production TLS stack. These boundaries must remain visible in commands, documentation and release notes.

## Publication Rule

Local builds and tests may proceed. Source changes, commits, pushes, releases, release assets and documentation publication require an explicit owner instruction to publish.
