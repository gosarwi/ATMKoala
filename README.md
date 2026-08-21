# ATMKoala

**ATMKoala** is an independent, freestanding experimental x86-64 operating system written primarily in C and assembly. It has no glibc or host operating-system runtime dependency. The active build boots through **Limine**, enters a project-owned long-mode kernel, and provides the framebuffer desktop **Exp** alongside a text console.

> **Experimental software.** Use the disk installer only with disposable virtual disks or media whose data has been backed up. The project has no production-support or data-recovery guarantee.

## v0.9 release

This repository contains the source, documentation and a locally QEMU-validated Limine BIOS+UEFI hybrid ISO:

| Artifact | Value |
|---|---:|
| ISO | `atmkoala-OS-v0.9-limine.iso` |
| SHA-256 | `b1c80957f85ad10637819f83e18b35b58e94d9ae3ef7d1c01a5b6aceb5fbadc1` |
| Kernel image | `build/kernel.bin` after `make all` |
| Boot path | Limine, Multiboot2, BIOS and UEFI hybrid ISO |
| Verification | `tests_qemu_linux_l0.sh` passed three consecutive local runs |

Verify the release image with:

```bash
sha256sum -c atmkoala-OS-v0.9-limine.iso.sha256
```

## Scope

| Area | Implemented, bounded capability | Important limitation |
|---|---|---|
| Kernel | Freestanding x86-64 kernel, custom paging, scheduler and VFS | Not a Linux kernel or a POSIX-certified system. |
| Boot | Limine v12.6.0 BIOS+UEFI hybrid ISO, Multiboot2 handoff | `ATMBOOT` and `ATMUEFI` are frozen legacy artifacts; the active workflow does not modify them. |
| Desktop | Exp framebuffer desktop, Terminal, Files, Notepad, Viewer, ArchiveEx, Calendar, Paint, games and Clock | No X11/Wayland server or retained compositor. |
| Clock | Local civil-time display for embedded IANA-style presets and a per-window PIT-based stopwatch | No NTP client, complete historic TZif data, or automatic legal-time-rule updates. |
| Storage | CatFS, FAT32, ext2 adapter and intentionally bounded btrfs facilities | Do not treat incomplete filesystem paths as production-safe. |
| Network | RTL8139, ARP, IPv4, bounded TCP client/listener primitives and native socket ABI | No TLS/HTTPS implementation or general-purpose network compatibility. |
| Runtime | Native ABI v1.12, syscall ABI v11 and bounded Linux x86-64 adapter L3 | No glibc, dynamic linking, `fork`, threads, complete signals or arbitrary Linux binaries. |

## Build

The build requires a GNU x86-64 toolchain, `xorriso`, `mtools`, `mkfs.fat`, QEMU and the usual POSIX shell utilities. Required Limine assets are included under `third_party/limine/`.

```bash
git clone https://github.com/gosarwi/ATMKoala.git
cd ATMKoala
make all
make iso
```

The generated image is `atmkoala-OS-v0.9-limine.iso`.

| Command | Purpose |
|---|---|
| `make all` | Builds `build/kernel.bin` and the embedded static-libc smoke application. |
| `make iso` / `make limine` | Builds the Limine BIOS+UEFI hybrid ISO. |
| `make run` | Starts the primary ISO in QEMU. |
| `make run-vbe` | Starts QEMU using the standard VBE device. |
| `bash tests_qemu_linux_l0.sh` | Boots the ISO, runs `posix test`, and checks required serial regression markers. |

## Time, timezone and Clock

The timezone selector and the `timezone` shell command accept the embedded supported identifiers, including `Asia/Yekaterinburg`, `Asia/Novosibirsk`, `Asia/Krasnoyarsk`, `Asia/Irkutsk`, `Asia/Yakutsk`, `Asia/Vladivostok`, `Asia/Magadan` and `Asia/Kamchatka`.

For normal hardware where the firmware RTC stores UTC, use the default basis:

```text
timezone Asia/Yekaterinburg
timezone clock utc
date
```

`date`, Exp Clock and Calendar then show selected local civil time and report the applied UTC offset/DST state. If a firmware RTC is intentionally maintained in local time instead, use `timezone clock local`; conversion is disabled so the raw local CMOS value is not shifted twice.

Exp Clock includes a stopwatch. Press **S**, Space or Enter to start/pause it; press **R** to reset it. The same controls are available by mouse. Stopwatch elapsed time uses monotonic 100 Hz PIT ticks and is independent of RTC availability and timezone changes.

> Timezone logic is a compact current-era embedded rule table, not a full IANA TZif history. IANA updates TZDB when jurisdictions change offsets or DST; update ATMKoala when a supported zone's civil-time law changes.[1]

## ATPK packages

The main OS source and release image are kept in **this repository**. Installable and incoming ATPK package artifacts live separately in [ATMKoala-ATPK-Packages](https://github.com/gosarwi/ATMKoala-ATPK-Packages). This separation prevents the OS release history from being mixed with package intake.

The existing `fastfetch-src-1.0.0-all.atpk` is intentionally an **incoming test artifact** only. Its `data/src/` layout is not supported by the current ATPK installer and it must not be represented as installable.

## Project layout

```text
boot/                 Multiboot2 entry and Limine configuration
src/                  Kernel, VFS, networking, desktop, native ABI and drivers
sdk/                  Freestanding native static libc and SDK material
third_party/          Vendored Limine, musl, Toybox and curl sources/notices
docs/                 Technical capability and compatibility records
assets/               Project visual assets
```

## License and third-party code

Original ATMKoala code is released under the [MIT License](LICENSE). Vendored third-party material retains its original license. Review [NOTICE.md](NOTICE.md) and component-specific vendor notices before redistributing modified copies.

## Documentation

- [POSIX compatibility and verified boundaries](docs/POSIX_COMPATIBILITY_STATUS.md)
- [Device capability status](docs/DEVICE_CAPABILITY_STATUS.md)
- [Detailed v0.9 overview in Russian](ATMKOALA_V09_DETAILED_OVERVIEW_RU.md)
- [Limine-only boot path](V09_LIMINE_BOOT_PATH_RU.md)
- [Native socket ABI](V09_USER_SOCKET_ABI_RU.md)
- [TLS/HTTPS scope](V09_TLS_HTTPS_SCOPE_RU.md)

## Contributing

Contributions should preserve the freestanding model, avoid glibc dependencies, retain third-party license boundaries, state limitations honestly, and include a build or QEMU regression whenever practical.

## References

[1] [IANA Time Zone Database](https://www.iana.org/time-zones)
