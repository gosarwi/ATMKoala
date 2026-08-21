# musl Linux ABI Reference Record

**Component:** musl libc reference assessment (no source import)

**Purpose:** Define a bounded Linux x86-64 user ABI compatibility layer that can eventually run selected statically linked musl applications on ATMKoala.

**Source/revision:** Official musl project website and official cgit repository, assessed 2026-08-20. The official site advertises musl 1.2.6 as the latest release at assessment time.

**Source URLs:**

- https://musl.libc.org/
- https://musl.libc.org/about.html
- https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT

**Original path:** Not applicable. No upstream source file has been copied, compiled, linked, or modified in this repository.

**License and notice:** musl is MIT-licensed as a whole. Its COPYRIGHT file also identifies limited portions under compatible permissive licenses. Any future import must record the exact upstream revision, original file path, applicable notice, and retained license text in a separate record.

**Why independent implementation is insufficient:** Compatibility requires matching the documented Linux x86-64 syscall calling convention, error convention, static process-start stack layout, ELF program-header semantics, and a sufficient syscall set. ATMKoala will implement these interfaces independently unless a narrow, explicitly recorded port is approved.

**Local changes:** None. This record documents reference study only.

**ABI and security review:** Existing ATMKoala user tasks use `int $0x80`, an ATM-specific syscall map, an `ET_EXEC`-only loader, a fixed user window, and a compact non-Linux initial stack. musl is built on the Linux syscall API and cannot be claimed runnable until Linux-style syscall entry and static startup prerequisites are implemented and tested.

**Build and QEMU/hardware tests:** None for musl itself. No imported musl code is part of the current image.

**Maintainer decision:** Approved as a technical and licensing reference only. Do not import musl source without a new per-component provenance record and explicit review.
