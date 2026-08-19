# ATMKoala v0.9: Mesa Foundation Boundary

## Current decision

ATMKoala does **not** port Mesa, Gallium, LLVMpipe, OpenGL, EGL, DRM, DRI or a Linux GPU driver in this increment. Those systems depend on significantly broader runtime, compiler, threading, memory-management and platform contracts than the freestanding kernel currently provides.

The implemented foundation is deliberately narrower: a kernel-owned **software render-target capability interface**. It identifies the available framebuffer, exposes TinyGL-Lite as a fixed-point software renderer, and reports absent hardware-acceleration and GPU-accounting capabilities explicitly. This gives future native graphics libraries a stable target/surface contract without falsely advertising Mesa compatibility.

| Capability | v0.9 foundation state |
|---|---|
| Framebuffer render target | Available through VBE/Multiboot framebuffer |
| Fixed-point triangle renderer | Available through TinyGL-Lite |
| Render target/capability query API | Implemented in `mesa_foundation.*` |
| OpenGL / OpenGL ES ABI | Not implemented |
| EGL / GLX / DRI / DRM | Not implemented |
| Gallium `pipe_screen` / `pipe_context` ABI | Not implemented |
| LLVM / llvmpipe JIT | Not implemented |
| Hardware GPU command submission | Not implemented |
| Per-process GPU memory/time | Not available; reported honestly |

> Mesa documents Gallium as a device-agnostic graphics-driver API with object-based core hardware services. LLVMpipe is a multithreaded LLVM JIT software rasterizer. Neither can be represented faithfully by a minimal framebuffer-only freestanding OS without first adding their required platform foundations.[1][2]

## Next prerequisites for an actual Mesa-oriented port

The next work would require a stable native C ABI beyond the static demo subset, user-mode threading and TLS, robust virtual-memory lifetime management, file/resource loading, a real graphics command and synchronization model, shader/compiler strategy, and a selected driver interface. Hardware acceleration additionally needs device-specific command submission, buffer management, MMU/IOMMU safety and reset/recovery rules.

## References

[1] [Mesa Gallium introduction](https://docs.mesa3d.org/gallium/intro.html)

[2] [Mesa LLVMpipe documentation](https://docs.mesa3d.org/drivers/llvmpipe.html)
