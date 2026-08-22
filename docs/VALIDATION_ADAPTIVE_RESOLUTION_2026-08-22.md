# ATMKoala Adaptive Resolution — Validation Record

**Scope.** This record covers the active Limine hybrid ISO source tree only. It documents the adaptive graphical boot work completed on 2026-08-22. No GitHub publication occurred.

## Implemented behavior

| Layer | Implemented behavior | Boundary |
|---|---|---|
| Limine BIOS and UEFI menu | The first entry, **Graphical — adaptive display**, requests `1920x1080x32`. Limine chooses another available graphical mode when that requested Multiboot2 mode is unavailable.[1] | This is a preferred bootloader mode request, not EDID/panel-native detection. The 1024×768 and 640×480 entries remain manual fallbacks. |
| VBE handoff | The kernel adopts only a sane 24/32-bpp RGB framebuffer with valid pitch/size arithmetic. A pure regression checks 24/32-bpp cases, pitch validation and the Exp minimum geometry. | No VBE BIOS calls, UEFI GOP calls, native GPU modesetting or runtime resolution switching were added. |
| Exp | The desktop already uses adopted `vbe.width`/`vbe.height` for physical-pixel bounds, taskbar, windows, launcher and mouse coordinates. It now refuses to launch below 640×480 while retaining VBE console fallback. | UI scale remains fixed at 100%. No automatic high-DPI scaling or per-monitor layout engine is claimed. |
| Backbuffer | The backbuffer size calculation is guarded against 32-bit overflow; failure to allocate still falls back to direct framebuffer rendering under the existing path. | This does not guarantee smooth performance at all high resolutions or on every physical GPU. |

> QEMU booted the new first entry with an adopted `1920x1080x32` framebuffer and reported `[vbe] geometry-ok`. This proves the configured preferred handoff in the test VM; it does **not** prove that every physical firmware chooses a screen’s exact native mode.

## Regression results

| Command | Runs | Result |
|---|---:|---|
| `make all` | Repeated during implementation and before final ISO | Passed |
| `make atmloader` | 1 final rebuild | Passed |
| `bash tests_qemu_linux_l0.sh` | 2 successful runs after one corrected pure-test assertion | Passed; includes required `[vbe] geometry-ok` marker and 1920×1080 preferred handoff in QEMU |
| `bash tests_qemu_ext2_write_type.sh` | 2 | Passed; guarded direct-block persistence unchanged |

## Final artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `build/kernel.bin` | 765,032 bytes | `03c4beee37443796aa5daacf90ff16c76780c1914402b6349308c921111699fa` |
| `atmkoala-OS-v0.9-limine.iso` | 6,154,240 bytes | `a33949c7526a1f3050e15883d613b7e9d3f99fa239b13d77151c1d27bd6f6b9e` |

## Publication status

The rebuilt ISO and source changes remain local. Publishing is a separate action and still requires a fresh explicit request.

## References

[1] [Limine, *Configuration File — Multiboot2 `resolution` option*](https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md)
