# Image Viewer QA Notes

## QEMU VBE baseline

The fixture-enabled ISO booted to Exp at 800×600 in QEMU. The captured framebuffer shows a stable dark Exp desktop, bottom taskbar, and focused terminal window without visible render corruption. The serial log reports a valid 32bpp VBE framebuffer and successful VBE initialization.

- Screenshot: `/tmp/atmkoala-image-boot.png`
- ISO: `atmkoala-OS-v0.5.iso`
- Fixture files expected at boot: `/home/exp-sample.png` and `/home/exp-sample.jpg`

## Input-route observation

The QEMU monitor requires one `sendkey` command per key. Entering `exit` in the Exp terminal cleanly returned to the text shell. The text shell’s `gui` command correctly reported that Exp must be opened with `de`; the next interactive step therefore uses `de` rather than `gui`.

## Files integration

Exp was restarted through the `de` command and its `Alt+F` shortcut opened the Files application. The root listing includes `/home` alongside the standard directories, confirming that the fixture seeding path is visible through the ordinary file-manager workflow. The selection begins on `modules`; the next action enters the visible `home` directory and opens a sample image from there.

## Decoder regression result — JPEG failure

Both seeded fixtures are visible in Files at `/home`: `exp-sample.jpg` (2 KiB) and `exp-sample.png` (2 KiB). However, selecting the JPEG through Files caused a reproducible kernel page fault. The panic screen reports exception 14 with `RIP=0x00000000002221a5`, `CR2=0x00000000`, heap usage 2,071,360 bytes, and a halted system. This is not accepted as a passing regression result; the next step is symbol-level diagnosis and correction in the decoder integration before retesting both formats.

## Decoder regression result — JPEG passes after TLS fix

After defining `STBI_NO_THREAD_LOCALS`, the exact same Files → `/home/exp-sample.jpg` workflow completed without a panic. The JPEG viewer displayed decoded 96×64 pixels with its checkerboard background, filename title and format/dimension footer. The original panic was therefore traced to unsupported compiler TLS access in the kernel, not corrupted JPEG input.

The `+` key reaches the viewer and changes its output, but QA revealed an interaction defect: the viewer changes from fit-to-window to literal 100% size, which makes a 96×64 image dramatically smaller. The control will be corrected to use meaningful relative magnification before this phase is accepted.

## Zoom regression result — corrected

The fit-relative zoom redesign passes visual QA. The fitted JPEG occupies the available viewport; one `+` renders the same decoded image at **125% of fit**, visibly magnified and safely clipped to the canvas. The footer reports the active relative zoom state. This corrects the earlier behavior that reduced small images to a literal 100% source-pixel size.

- Fit capture: `/tmp/atmkoala-image-jpeg-fit-corrected.png`
- Zoom capture: `/tmp/atmkoala-image-jpeg-zoom-corrected.png`

## PNG regression result — passes

The normal Files → `/home/exp-sample.png` path successfully opened the PNG in Image Viewer. The 96×64 PNG renders distinct from the JPEG fixture, including its alpha-aware checkerboard treatment, and reports `PNG 96x64 fit` in the footer. At `125% of fit`, the decoded PNG is magnified and clipped correctly without a panic. The subsequent `0` reset capture was produced after returning to fit mode.

- Fit capture: `/tmp/atmkoala-image-png-fit-corrected.png`
- Zoom capture: `/tmp/atmkoala-image-png-zoom-corrected.png`
- Reset capture: `/tmp/atmkoala-image-png-reset-corrected.png`

## Exp chrome visual QA

The refined neutral chrome renders successfully in QEMU: layered shadows, focused peach divider and framed controls are visible without changing the independent VGA console palette. The About product card is also drawn correctly. However, the first default-scale capture exposed overflow from several long About strings at 130% UI scale. The text will be shortened and reflowed to preserve the promised scale range rather than accepting clipped copy.

## POSIX portable-userland regression

`posix test` was executed in a fresh QEMU boot and returned `paging=OK uaccess=OK vfs-posix=OK`. The expanded test now covers task-local cwd/umask context, lexical `.`/`..` path normalization, `creat`, vectored reads/writes, positioned reads and persistence synchronization. The BusyBox-oriented port contract is now ABI 1.1 and advertises these verified stream/context facilities; `fork`, `execve`, pipes, full signals and Linux-specific interfaces remain explicitly deferred because they do not yet have a real isolated process/IPC implementation.

