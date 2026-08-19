# ATMKoala Native GUI ABI v1

## Status

`SDK/atm_gui.h` is the public v0.9 **client data contract** for statically linked ATM applications. It replaces any assumption that `exp.h`, AWM structures or in-kernel Exp callbacks are stable third-party interfaces.

The ABI is versioned as `1.0`. Its coordinate system is permanently **physical VBE pixels at 100%**. No application must derive layout from user-selected scaling, and `scale_percent` in legacy Exp callbacks remains `100` only for compatibility.

| Component | v0.9 state |
|---|---|
| ABI version negotiation | Available through `atm_gui_runtime_info`. |
| Public handle, rectangle, window/surface/event layouts | Defined in `sdk/atm_gui.h`. |
| Static libc linkage | Available; `atm_gui_stub.c` is included in the native smoke application. |
| Current capabilities | `0`: there is no exposed compositor transport yet. |
| Window/surface/event operations | Linkable but return `-1` with `errno=ENOSYS`. |
| Existing Exp ABI | Remains trusted in-kernel/module callback API, not a CPL 3 GUI transport. |

## Client protocol

A native program starts by passing a writable `atm_gui_runtime_info_t` to `atm_gui_runtime_info`. It must verify the ABI major number, `struct_size`, and requested capability bits before calling any resource-creating operation. In this milestone it observes a valid v1 runtime record with zero capabilities; it can disable GUI behaviour without relying on unresolved symbols, private addresses or kernel-version heuristics.

```c
atm_gui_runtime_info_t gui;
if (atm_gui_runtime_info(&gui) == 0 &&
    (gui.abi_version >> 16) == ATM_GUI_ABI_MAJOR &&
    (gui.capabilities & ATM_GUI_CAP_WINDOWS)) {
    /* Future compositor path. */
} else {
    /* v0.9: console or non-GUI fallback. */
}
```

## ABI rules

| Rule | Contract |
|---|---|
| Versioning | An incompatible major version changes the high 16 bits of `abi_version`. Additive minor changes must preserve old structure prefixes. |
| Structure sizing | Every extensible descriptor begins with `struct_size`; clients initialize it to `sizeof(struct)`. |
| Handles | Handles encode slot and generation. A future server must reject stale generation values rather than reusing an old object silently. |
| Pixels | Surface format v1 is `XRGB8888`; channel/stride layout is explicit in `atm_gui_surface_desc_t`. |
| Damage | Present accepts an explicit rectangle array; null damage may later mean full-surface damage. |
| Errors | Unsupported operations return `-1` and set the native single-threaded `errno`. |
| Scaling | All geometry uses 100% physical pixels; no fractional or persisted scale setting exists. |

## Next implementation gate

The declarations intentionally precede the compositor implementation. The next increment must first add a process-to-compositor event mechanism and a controlled shared-buffer/memory mapping primitive. Only then may `ATM_GUI_CAP_WINDOWS`, `ATM_GUI_CAP_SURFACES`, `ATM_GUI_CAP_DAMAGE` and `ATM_GUI_CAP_INPUT` become non-zero. This staging prevents the current trusted AWM callback machinery from accidentally granting arbitrary CPL 3 clients access to the kernel framebuffer.
