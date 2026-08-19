# ATMKoala v0.9 Development Baseline

## Release assets

* `atmkoala-OS-v0.9-limine.iso`
* `atmkoala-OS-v0.9-limine.iso.sha256`

This release is a **Limine-only** BIOS+UEFI hybrid ISO. It replaces the active GRUB ISO workflow with Limine v12.6.0 while retaining the existing Multiboot2 kernel handoff.

## Included and verified

| Area | Release state |
|---|---|
| Boot | Limine BIOS and UEFI boot paths verified in QEMU/OVMF through kernel VBE initialization. |
| POSIX boundary | Paging, user-copy, VFS POSIX functions, syscall user-copy, task-local FDs, native CPL 3 and static libc smoke tests pass. |
| Native libc | Static freestanding API with musl-derived string, ctype and integer-conversion subset. |
| Desktop | Exp framebuffer desktop with terminal, Notepad, Files, Viewer and ArchiveEx. |
| Networking | RTL8139, ARP, IPv4, TCP primitives and native sockets. |

## Important limitations

This is experimental software. The disk installer is destructive and should only be used on disposable media. TLS/HTTPS is not implemented. The ext2 VFS adapter does not provide transactional metadata changes, so create/mkdir is intentionally not advertised as complete.

## Integrity

Verify the ISO with:

```bash
sha256sum -c atmkoala-OS-v0.9-limine.iso.sha256
```

## Licensing

Original ATMKoala code is MIT-licensed. Vendored Limine, musl, Toybox and curl components retain their upstream license terms; see `NOTICE.md`.
