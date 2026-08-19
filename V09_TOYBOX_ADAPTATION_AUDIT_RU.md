# Toybox 0.8.14 — первичный аудит адаптации к ATMKoala v0.9

## Зафиксированный upstream

| Поле | Значение |
|---|---|
| Repository | `landley/toybox` |
| Version | `0.8.14` |
| Commit | `b7ec52ac35e075caffca5d330995d44e8dbfc8c3` |
| Local vendor path | `third_party/toybox/` |
| License | Upstream permissive ISC/0BSD-style `LICENSE` |

Source tree сохранён неизменённым. Проектная запись provenance находится в `third_party/toybox/ATMKOALA_VENDOR.md`. `boot/ATMBOOT` и `boot/ATMUEFI` не затрагивались.

## Что подтверждено в исходниках

Toybox реализует exactly the desired multicall pattern: `main.c` finds an applet by `argv[0]`, and when binary invoked as `toybox`, `toybox_main()` treats its first argument as applet name. Его dispatcher генерируется из `NEWTOY` declarations, uses global context, option parser, help infrastructure, locale set-up and stdio buffering. These hosted assumptions cannot be linked directly against current `sdk/libc`.

| Applet | Direct upstream dependencies discovered | ATM v0.9 feasibility |
|---|---|---|
| `true`, `false` | Dispatcher and exit status only | First porting target. |
| `echo` | `printf`, `putchar`, UTF-8/wide-character helpers, option parser | Requires reduced output/escape helper; UTF-8 conversion deferred. |
| `printf` | `sprintf`, numeric/float conversion, `strtold`, option parser | Start with native `%s`, `%d`, `%%`; float formats deferred. |
| `cat` | `loopfiles`, `read`, `xwrite`, error helpers | Fits current `open/read/write/close` after native loop helper. |
| `wc` | `fstat`, wide-char classification, UTF-8 helpers, `loopfiles` | Byte/line/ASCII-word mode feasible; needs ABI `fstat` wrapper. |

## Adaptation decision

Do not attempt to compile Toybox’s whole Kconfig-generated framework or Linux-oriented common `lib` against freestanding ATM libc. Instead create `sdk/toybox_atm/`: a narrow compatibility runtime which preserves the upstream command model and adapted applet semantics but replaces hosted components with ATM implementations.

The target binary will be called `toybox.elf` and be statically linked with `sdk/libc` as `ET_EXEC`. It will accept the canonical `toybox <applet> [args...]` form when the native loader gains argv construction. Before that, `--selftest` will exercise the dispatcher in CPL 3 using synthetic argv within the process.

## Required ABI work before a useful first fixture

| Needed feature | Current status | Intended use |
|---|---|---|
| `argv`/`envp` stack constructed by native spawn | Absent; loader provides `argc=0` | External multicall invocation. |
| Reliable `fstat` declaration/wrapper in libc | Kernel syscall exists; public libc surface incomplete | `wc`, later `ls`. |
| Write-based format/output helper | Absent | `echo`, `printf`, diagnostics. |
| Narrow `loopfiles` replacement | Absent | `cat`, `wc`. |
| Wide-character and locale system | Absent by design | Explicitly out of first scope. |

## Outcome

A direct build of upstream Toybox is not a meaningful next step: it would fail at its expected hosted libc, generated configuration and Linux/Android facilities. A small static Toybox-compatible adaptation is technically appropriate and can reuse semantics in a controlled applet-by-applet fashion while retaining upstream attribution.

## References

[1]: https://github.com/landley/toybox/blob/master/LICENSE "Toybox LICENSE"
[2]: https://landley.net/toybox/ "Toybox project overview"
[3]: https://landley.net/toybox/faq.html "Toybox FAQ and architecture walkthrough"
