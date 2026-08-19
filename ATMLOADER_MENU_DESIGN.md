# ATM Loader — common BIOS/UEFI boot menu

ATMBOOT and ATMUEFI remain firmware-specific transport layers, but expose the same **ATM Loader 2×2 grid** and the same selection semantics before handing off to the shared x86-64 kernel.

| Key / tile | Label | Kernel handoff | Result |
|---|---|---|---|
| `1` or Enter | **EXP DESKTOP** | Framebuffer tag; no cmdline override. | Normal graphical Exp desktop. |
| `2` | **COMPAT CONSOLE** | Multiboot2 cmdline tag: `novbe`; no framebuffer tag. | VGA/text-compatible console, safe on graphics failures. |
| `3` | **DISK INSTALLER** | Framebuffer tag + Multiboot2 cmdline tag: `installer`. | Existing graphical disk installer boot entry. |
| `4` | **RESTART** | Firmware reset; no kernel handoff. | BIOS reset vector or UEFI cold reset. |

The UI is intentionally rendered with native firmware facilities: BIOS uses INT 10h text cells prior to VBE setup; UEFI uses Simple Text Output before GOP/kernel takeover. Both expose direct numeric selection and Enter-as-default, avoiding a dependency on device-specific pointer input.

The mode is represented only through standard Multiboot2-compatible tags already parsed by the kernel. This avoids diverging BIOS/UEFI kernel protocols. `novbe` retains the existing compatible-text behavior; `installer` retains the existing installer entry. No hidden diagnostics or destructive test functionality is exposed by the boot menu.

At the selection boundary both load paths are named **ATM Loader**. Firmware is shown as a small status line (`BIOS / ATMBOOT` or `UEFI / ATMUEFI`) rather than presenting the implementations as competing boot systems.
