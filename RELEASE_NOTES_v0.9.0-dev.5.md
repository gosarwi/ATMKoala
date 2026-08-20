# ATMKoala v0.9.0-dev.5

This development increment completes the current installer, Exp desktop, shell, TinyGL, diagnostics, and user-session work while preserving the project’s freestanding x86-64 design. It remains experimental software intended for virtual machines, disposable media, and development hardware.

## Implemented changes

The standalone Disk Installer now has pointer controls for cancellation, disk navigation, partition-plan adjustments, timezone selection, password focus, log closure, installation, and completion. The destructive action still requires the user to type `ERASE`; no pointer interaction bypasses that confirmation. Installer timezones now expose 37 IANA-style presets, while the system honestly states that wall-clock conversion awaits RTC or NTP support.

Exp now provides user-created Tasks, task deletion, and CatFS Trash handling through `/data/uiu/Trash`. The session flow lists active accounts when `login` is called without a name, greets a successful login with `Hello, NAME!`, uses `Hello, root!` at startup, and uses `atmkoala` as the bootstrap password for newly initialized accounts.

TinyGL-Lite has separate CPU-rendered cube and gears scenes. The `cube`, `gears`, and `glxgears` shell commands open the matching scene; `C` and `G` switch between scenes in the TinyGL window. The renderer is explicitly software-only and does not implement GLX, Mesa, or hardware acceleration.

Calculator now evaluates bounded integer expressions with unary signs, `+`, `-`, `*`, `/`, `%`, whitespace, parenthesized subexpressions, and precedence. The `calc` command launches it, while `bc` is an explicitly limited compatibility launcher rather than an upstream GNU bc port. Likewise, `nano [path]` opens the native editable Notepad surface and is not presented as an upstream GNU nano port.

The new `gpu` command inventories PCI display controllers and reports actual framebuffer-helper status without claiming native acceleration. The `swap` command lists primary MBR type `0x82` candidates but explicitly reports that page eviction, swap activation, backing I/O, and a virtual-memory pager are not implemented. `lsblk` identifies the same MBR type as `Linux-swap`.

## Validation

The release candidate was rebuilt using `make all` and `make iso`. QEMU regressions covered the project POSIX self-test, default Exp startup, the dedicated Limine Disk Installer route, safe `swap` and `gpu` diagnostics, the TinyGL cube scene, Calculator expression `2*(3+4)%5 = 4`, and the Nano-style Notepad alias. The POSIX result is a passing project compatibility self-test, not a claim of complete POSIX conformance.

## Important limitations

Swap paging, USB mass storage, AHCI/NVMe block drivers, hardware GPU acceleration, Mesa/GLX, TLS/HTTPS, and complete POSIX conformance remain outside this development increment. The Disk Installer is destructive and should never be used on media containing data that has not been independently backed up.
