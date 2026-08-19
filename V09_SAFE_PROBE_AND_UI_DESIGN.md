# ATMKoala v0.9 — safe probe and fixed UI design

## Hardware-probe contract

Interactive commands must report data collected during safe boot initialization; they must not start new timer calibration, raw EC transactions or model-specific register reads. CPUID remains permitted because it is a defined enumeration instruction, but every optional leaf must be gated by the maximum basic or extended leaf. CPUID leaf 0 exposes the maximum basic input value, and unsupported input handling is processor-specific enough that the kernel should still avoid unenumerated leaves.[1]

RDMSR cannot be treated as safe merely because a local helper uses that name. An unimplemented MSR address causes #GP(0), and MSR sets vary by processor family.[2] The v0.9 safe path removes the `IA32_PLATFORM_INFO` read from common CPU detection rather than attempting exception recovery inside a still-maturing kernel.

| Operation | New policy | Reason |
|---|---|---|
| CPUID leaves 0/1/4/7/ext brand | Enumerated and bounded. | Architectural, no device I/O. |
| CPU frequency | CPUID leaf `0x15` ratio + crystal when usable; otherwise leaf `0x16` base MHz; otherwise zero/unknown. | Avoid blocking PIT calibration. |
| MSR 0x198 | Not used in common detection. | Reserved/unimplemented MSR can #GP. |
| ACPI EC ports 0x62/0x66 | Disabled by default. | OEM-specific register map and synchronous I/O. |
| `hwinfo` | Prints cached `g_cpu`/boot data only. | Must be non-blocking after boot. |
| `de` | No pre-launch sleep; no raw hardware polling in frame loop. | First visual frame must not depend on IRQ or EC progress. |

## Fixed 100% Exp layout contract

`exp_ui_scale_pct` is an internal compatibility constant equal to `100`; it is no longer persisted, user-configurable or exposed in Settings. `EXP_SCALE(v)` and `EXP_UNSCALE(v)` become identity mappings, and `DE_SCR_W/H` are the physical VBE framebuffer dimensions. Existing native Exp callbacks receive `scale_percent=100` for ABI continuity, but applications must not request another scale.

| Surface | Decision |
|---|---|
| Runtime scale | Always 100%. |
| Config key `desktop.ui_scale` | Ignored; new values are not written. |
| Settings UI | Removes scale buttons, keyboard brackets and scale status. |
| Mouse coordinates | Use native framebuffer pixels, no unscale transform. |
| External GUI context | `scale_percent=100` fixed for ABI v1 compatibility. |

## References

[1] [CPUID — CPU Identification](https://www.felixcloutier.com/x86/cpuid)

[2] [RDMSR — Read From Model Specific Register](https://www.felixcloutier.com/x86/rdmsr)
