# musl vendor provenance for ATMKoala

| Field | Value |
|---|---|
| Upstream | `https://git.musl-libc.org/git/musl` |
| Pinned commit | `f21a96538f78fa8e2040831b4209b35f2fb581da` |
| Upstream describe | `v1.2.6-20-gf21a9653` |
| Retrieved | 2026-08-19 |
| License | MIT; retain upstream `COPYRIGHT` notices |

## ATMKoala status

The source tree is retained unmodified for standards and implementation audit. Upstream musl is a libc built over the Linux syscall layer; it cannot be configured or linked as an ATMKoala libc without a dedicated syscall/threads/dynamic-linker port. ATMKoala continues to implement a **musl-compatible static subset** over the native `int $0x80` ABI, adding headers and routines incrementally with CPL3 smoke coverage.

Reusable code, if selected later, must retain exact upstream copyright/license text and carry a per-file adaptation record. No upstream musl code has been compiled or copied into the current ATM runtime at this checkpoint.
