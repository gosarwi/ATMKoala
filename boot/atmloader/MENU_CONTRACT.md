# ATM Loader menu contract

The **ATM Loader** project is the unified boot-facing contract for ATMKoala. It does not claim that BIOS real-mode code, UEFI PE/COFF code, and Limine are one binary. They remain separate execution paths because firmware interfaces, binary formats, storage discovery, and mode-transition requirements differ.

All maintained paths expose the following selection semantics before the kernel begins:

| Selection | Kernel command line | Display intent | Safety boundary |
|---|---|---|---|
| Graphical 800×600 | none | 32-bit framebuffer at 800×600 | Graphics availability remains firmware/loader dependent. |
| Graphical 1024×768 | none | 32-bit framebuffer at 1024×768 | Available in Limine paths; bare fallback may select its supported VBE/GOP mode. |
| Graphical 640×480 | none | 32-bit framebuffer at 640×480 | Available in Limine paths; bare fallback may select its supported VBE/GOP mode. |
| Text Mode | `novbe text` | Text-safe kernel startup | Never claims a framebuffer. |
| Disk Installer | `installer` | Graphical installer startup | The installer itself owns all destructive confirmation and target-disk checks. |
| Restart | firmware reset | Bare-loader-only convenience | No kernel handoff occurs. |

## Execution paths

| Path | Artifact | Firmware boundary | Kernel handoff |
|---|---|---|---|
| Hybrid distribution | `atmkoala-OS-v0.9-limine.iso` | Limine BIOS and UEFI binaries | Multiboot2 magic plus Multiboot2 information pointer. |
| ATMBOOT fallback | `atmkoala-atmboot.img` | BIOS MBR → EDD/VBE/ATA PIO | Multiboot2-compatible information block. |
| ATMUEFI fallback | `atmkoala-atmuefi.img` | UEFI Simple File System/GOP → ExitBootServices | Multiboot2-compatible information block in long mode. |

The hybrid distribution is the primary release artifact. ATMBOOT and ATMUEFI remain independently built recovery/development images rather than a promise that every firmware path has identical mode-setting support.

## Limine attribution

The hybrid BIOS/UEFI path vendors Limine v12.6.0 binaries under their BSD-2-Clause terms. Its copyright and license text remain in `third_party/limine/bin/LICENSE`; source and binary redistribution must retain that attribution and disclaimer. The ATM Loader contract does not copy Limine source into ATMBOOT or ATMUEFI.
