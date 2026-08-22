# ATMKoala Adaptive Backbuffer Anti-Flicker Fix — Validation Record

**Scope.** This record covers the active Limine hybrid ISO source tree only. It documents the Exp flicker fix completed on 2026-08-22. No GitHub publication occurred.

## Cause and implementation

At the adaptive preferred `1920x1080x32` boot mode, a full RGB framebuffer consumes approximately 7.9 MiB before pitch padding. The general kernel heap is intentionally 8 MiB and is already used by normal system allocations. The previous Exp startup silently ignored a `vbe_double_buffer_enable()` allocation failure; direct framebuffer redraw was then permitted, making whole-window erase/redraw visible as flicker.

| Item | Implemented result |
|---|---|
| Dedicated surface | VBE owns one bounded 10 MiB BSS backbuffer for full-frame rendering when the adopted surface fits. This avoids competing with the general 8 MiB heap at the adaptive 1920×1080 mode. |
| Atomic presentation | Exp draws into the dedicated surface and copies a complete frame only in `vbe_present()`. |
| Safe fallback | A mode that exceeds the bounded surface or cannot obtain a dynamic fallback remains non-fatal; Exp visibly warns that direct rendering may tear. |
| Regression telemetry | Exp emits `[vbe] exp-backbuffer-static` when the adaptive default path uses the reserved surface. The primary QEMU harness now requires this marker. |
| State safety | The VBE fast-path selftest preserves and restores static-backbuffer origin together with its other temporary state. |

> This fix targets full-frame redraw flicker caused by the former high-resolution heap-allocation failure. It does not claim a universal solution for firmware/GPU scanout artifacts, invalid physical framebuffer handoff, or performance of modes larger than the bounded reserve.

## Verification

| Command | Runs | Result |
|---|---:|---|
| `make all` and `make atmloader` | 1 final rebuild | Passed |
| `bash tests_qemu_linux_l0.sh` | 2 | Passed; adaptive first entry reached 1920×1080 and required `[vbe] geometry-ok` plus `[vbe] exp-backbuffer-static` |
| `bash tests_qemu_ext2_write_type.sh` | 2 | Passed; guarded Ext2 direct-block persistence unchanged |

## Final artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `build/kernel.bin` | 765,232 bytes | `17c8eb847ec83b47415ddaa358dc16154f721cf89fffa03fd856bb8f2fbec051` |
| `atmkoala-OS-v0.9-limine.iso` | 6,154,240 bytes | `a3a02e1cc5569a22c3fbd81d6431b0986b7f421a9ab73fd39693f914b8e6e533` |

## Publication status

The source and ISO remain local. Publishing is a separate action requiring a fresh explicit user request.
