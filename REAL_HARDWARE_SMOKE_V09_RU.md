# ATMKoala v0.9 — smoke-проверка на реальном железе

## Назначение

Эта последовательность проверяет исправление зависаний `hwinfo` и `de`, fixed 100% Exp layout, а также безопасный режим hardware telemetry. Она не требует raw EC, MSR или TSC calibration probes.

## Подготовка

Запишите **один** из образов на тестовый USB-носитель: `atmkoala-OS-v0.5.iso` для GRUB fallback, `atmkoala-atmboot.img` для BIOS/Legacy или `atmkoala-atmuefi.img` для UEFI ESP. На первом запуске не используйте production media with unsaved data. Выберите **EXP DESKTOP** from ATM Loader.

## Проверки

| Step | Action | Expected result |
|---|---|---|
| 1 | Wait for Exp terminal. | Desktop appears at its physical VBE resolution; UI layout is 100%. |
| 2 | Type `hwinfo` and press Enter. | Report returns promptly to the prompt. It may show `TSC unavailable (no calibrated timer probe)`; this is valid. |
| 3 | Press `Alt+F1`, then type `de`. | Exp returns without a fixed multi-second wait or a freeze. |
| 4 | Press `Alt+S`. | Appearance page says `Interface scale: fixed at 100% for stable layout`; no scale buttons are offered. |
| 5 | Leave the system idle for at least 15 seconds. | Exp remains responsive. Battery telemetry may say unavailable; that is intentional until an ACPI driver validates the actual board. |
| 6 | Return to shell with `Alt+F1`, run `posix test`. | Existing paging, user-copy, process-FD, native-CPL3 and static-libc checks remain `OK`. |

## If an issue remains

Photograph the last screen and record: firmware type (BIOS/UEFI), device model, boot image, resolution, command, and whether `Alt+F1` still works. Do not attempt to enable raw EC or arbitrary MSR reads manually; this release intentionally suppresses those unvalidated probes.
