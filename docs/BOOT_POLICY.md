# Boot policy

## Active development path

**Limine hybrid ISO is the only active boot-development and release path for ATMKoala.** The supported artifact is:

```text
atmkoala-OS-v0.9-limine.iso
```

It contains Limine BIOS and UEFI loading paths and starts the existing kernel through its Multiboot2 header. The canonical build commands are:

```sh
make all
make iso
# equivalent Limine-only project alias
make atmloader
```

The UEFI menu inside the Limine El Torito FAT volume and the BIOS-facing hybrid menu provide the same five choices: 800×600, 1024×768, 640×480, text-safe `novbe text`, and Disk Installer `installer`.

## Archived legacy images

`boot/atmboot` and `boot/atmuefi` are retained only as archived recovery/development references. They are not deleted, but no new platform, kernel, desktop, POSIX, filesystem or graphics functionality is added to them. Their manual build target is:

```sh
make legacy-bootloaders
```

These artifacts are not part of `make atmloader` and are not a release claim. Their graphics support, device discovery and firmware behavior can differ from the Limine hybrid ISO.

## Limine attribution

The primary ISO vendors Limine v12.6.0 under BSD-2-Clause. Required copyright and disclaimer text is preserved in `third_party/limine/bin/LICENSE`, and the local vendor record is in `third_party/limine/ATMKOALA_VENDOR.md`.

> The policy does not assert that Limine, ATMBOOT and ATMUEFI are one binary. BIOS MBR code, UEFI PE/COFF code and Limine executables necessarily use different firmware interfaces. The supported unification is the single Limine hybrid distribution artifact and its common Multiboot2 menu semantics.
