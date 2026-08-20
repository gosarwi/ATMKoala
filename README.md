# ATMKoala

**ATMKoala** is a freestanding x86-64 experimental operating system written primarily in C and assembly. It runs without glibc or a host operating-system runtime, boots through **Limine**, enters a custom long-mode kernel, and provides a framebuffer desktop named **Exp** alongside a text console.

> This is an experimental OS project. It is intended for virtual machines, development hardware, and research. Do not use the disk installer on media containing important data.

## Current release

The current public build is **ATMKoala v0.9 development baseline**. The canonical boot artifact is:

```text
atmkoala-OS-v0.9-limine.iso
```

The ISO contains a Limine BIOS and UEFI boot path. GRUB is not part of the active source build or release workflow.

| Area | Current status |
|---|---|
| Bootloader | Limine v12.6.0; BIOS and UEFI hybrid ISO |
| Kernel | Freestanding x86-64, Multiboot2 handoff, custom paging and scheduler |
| User mode | Static ELF64 ET_EXEC applications at CPL 3 through `int $0x80` |
| Native libc | Static freestanding subset with musl-derived string, ctype and stdlib components |
| Desktop | Exp framebuffer desktop, terminal, Notepad, Files, Viewer and ArchiveEx |
| Storage | ATA PIO disk discovery; MBR primary-partition inspection; CatFS mount/fsck for primary partitions; CatFS and FAT32; ext2 read-only VFS adapter with guarded existing-block write support |
| Networking | RTL8139, ARP, IPv4, TCP client/listener primitives and native socket ABI |
| Native userspace | `atm-box` Toybox-compatible applets for files, process listing, memory, I/O, GPU and uptime reporting |
| Process accounting | Per-task CPU ticks, resident mapped bytes, native FD read/write bytes and context switches |
| Graphics foundation | Truthful framebuffer/TinyGL-Lite capability query and a fixed-point software gears demo; this is not a Mesa, OpenGL, GLX, EGL, Gallium, DRM or DRI port |
| USB | PCI USB-host controller discovery only; USB device enumeration and BOT/SCSI mass-storage are not implemented |
| TLS/HTTPS | Not implemented; curl remains audit-only |

## Build

The build expects a GNU toolchain, `xorriso`, FAT tools (`mtools`, `mkfs.fat`), and the usual QEMU development tools. Limine v12.6.0 assets are vendored under `third_party/limine/`.

```bash
git clone https://github.com/OWNER/ATMKoala.git
cd ATMKoala
make all
make iso
```

The output is `atmkoala-OS-v0.9-limine.iso`.

| Command | Purpose |
|---|---|
| `make all` | Build `build/kernel.bin` and the embedded static libc smoke application. |
| `make iso` | Build the Limine BIOS+UEFI hybrid ISO. |
| `make limine` | Alias for the Limine ISO build. |
| `make run` | Start the primary Limine ISO in QEMU. |
| `make run-vbe` | Start QEMU with the standard VBE device. |
| `make run-limine` | Explicit Limine QEMU runner. |

## Boot modes

The Limine menu provides graphical profiles at `800x600`, `1024x768`, and `640x480`, a Text Mode entry, and a destructive Disk Installer entry. The kernel remains a Multiboot2 executable: Limine passes the standard Multiboot2 information structure, then the project-owned early assembly enables long mode and enters the kernel.

`ATMBOOT` and `ATMUEFI` are frozen, independent legacy artifacts. They are not modified by the Limine ISO workflow.

## Verification

The current development regression baseline is:

```text
posix test: paging=OK uaccess=OK vfs-posix=OK
            syscall-usercopy=OK process-fd=OK
            native-cpl3=OK static-libc=OK image-bmp=OK
```

The static libc smoke application covers CRT startup, allocation, musl-derived string and ctype functions, integer parsing, binary search, GUI ABI checks, `fstat`, and native socket lifecycle operations. The Text Mode regression also checks process launch/reap, native user-copy, and the bounded 24-bit BMP decoder path; `atm-box free`, `atm-box ps`, `atm-box iostat` and `atm-box gpuinfo` expose the corresponding implemented telemetry surfaces.

## Storage, installer, and image wallpaper update

`lsblk` reports detected ATA PIO disks, validates primary MBR entries, and labels CatFS and ext2 signatures without mounting or writing them. `lsblk --rescan` is available only while CatFS is unmounted. The `usb` command reports PCI USB host controllers, but does **not** expose USB sticks as disks: the required USB enumeration, bulk-only transport, and SCSI block layer remain unimplemented.

The dedicated graphical installer is intentionally destructive. It selects one ATA PIO target, displays a cached MBR/filesystem preflight, lets the user choose a 1 MiB-aligned single CatFS layout, asks for a timezone preset and a hidden root password, and still requires typing `ERASE` before it replaces the primary MBR table. The password is stored only as the existing salted hash in CatFS; global configuration now follows the durable `/data/uiu/etc` path when CatFS is mounted. Press `L` at any installer stage to inspect its bounded read-only journal; after a successful format it is persisted at `/data/uiu/var/log/installer.log` and can be opened with `installer-log`. It creates no EFI System Partition and installs no bootloader. A reboot discovers the resulting CatFS primary partition and mounts it at `/data`.

`mount hda1` and `fsck hda1` now address detected CatFS primary partitions; `fsck -y hda1` is refused while CatFS is mounted. The file-only `dd` command supports streaming `if=`, `of=`, `bs=`, `count=`, `skip=`, `seek=`, and `conv=notrunc` through VFS paths. It deliberately does not expose raw-disk writes. `timezone` stores bounded IANA-style identifiers, while `date` truthfully remains an uptime display until RTC/NTP wall-clock support exists. The `mouse` command reports PS/2 auxiliary-port initialization status, packet counters, and overflow drops; USB/HID mouse transport is not yet present.

Use `gears` to open the TinyGL-Lite fixed-point software gears scene in Exp. `glxgears` is an explicit compatibility alias that explains that GLX/Mesa APIs are unavailable before opening the same software demo.

Exp supports persistent image wallpapers from **PNG**, **JPEG**, and uncompressed **24/32-bit BMP** files. Open an image in Viewer and press `A`, or use `wallpaper /path/to/image` in the Exp terminal. The image is decoded once, cached separately from Viewer windows, aspect-preserved with cover scaling, and rendered through the VBE backbuffer. The VBE full-frame copy and filled-rectangle paths were also converted from bytewise/per-pixel public calls to direct memory/row writes, while mouse-only desktop redraws are coalesced. The taskbar and title overlays now use bounded alpha compositing over the VBE backbuffer; **full blur is not implemented** because Exp has no retained compositor. The Menu glyph, battery, Wi-Fi, and Bluetooth status glyphs were refined without inventing unavailable telemetry.

The desktop now includes a navigable **manual Calendar** window. It has Gregorian month/leap-year calculations and arrow-key month/year navigation, but it never claims a live date: an RTC or NTP source remains required. `net drivers` reports every detected PCI Ethernet controller and its real driver state. RTL8139 uses the complete boot PCI inventory rather than a bus-0/function-0 assumption; Intel Ethernet, RTL8168/8125, and virtio-net controllers may be reported as detected but remain unsupported until their drivers exist.

## Project layout

```text
boot/                 Multiboot2 entry, Limine configuration, frozen legacy paths
src/                  Kernel, VFS, networking, desktop, native ABI and drivers
sdk/libc/             Freestanding native static libc and smoke application
third_party/          Vendored Limine, musl, Toybox and curl sources/notices
docs/                 GitHub Pages landing page
```

## Licensing

Original ATMKoala code is released under the [MIT License](LICENSE). Third-party components keep their original licenses; see [NOTICE.md](NOTICE.md) and their vendor records before redistribution.

## Documentation

* [Limine-only boot path](V09_LIMINE_BOOT_PATH_RU.md)
* [Detailed v0.9 overview (Russian)](ATMKOALA_V09_DETAILED_OVERVIEW_RU.md)
* [Native socket ABI](V09_USER_SOCKET_ABI_RU.md)
* [TLS/HTTPS scope and prerequisites](V09_TLS_HTTPS_SCOPE_RU.md)
* [ATMBox portable userspace profile](V09_ATMBOX_PROFILE_RU.md)
* [Mesa foundation scope and non-goals](V09_MESA_FOUNDATION_SCOPE_RU.md)

## Contributing

Contributions should preserve the freestanding build model, avoid glibc dependencies, preserve third-party license boundaries, and include a build or QEMU regression where practical.
