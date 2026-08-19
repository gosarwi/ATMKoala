# BusyBox portability sources for ATMKoala

## Primary sources consulted

1. BusyBox FAQ — https://busybox.net/FAQ.html
   - Describes BusyBox as a collection of programs rather than a complete system.
   - States that full functionality is tested on Linux with uClibc or glibc, and that newlib/libgloss support is experimental.
   - Documents static/cross builds and BusyBox links installation model.

2. BusyBox manual — https://busybox.net/downloads/BusyBox.html
   - Defines BusyBox as a configurable multi-call binary, commonly installed with command links.
   - States that it provides a fairly complete POSIX environment for small embedded systems, in the usual Linux system context.

3. BusyBox mirror README — https://github.com/mirror/busybox
   - Describes configurable applets, `busybox.links`, multi-call invocation and standalone shell.
   - States that upstream is developed and tested on Linux with GCC and uClibc/glibc. It explicitly explains that portability varies by applet and that Linux-specific applets such as module management need Linux kernel/C-library support.

## ATMKoala consequence

ATMKoala must not claim that upstream Linux BusyBox binaries run unchanged. The immediate target is a **BusyBox-oriented native portable profile**: static, source-adapted applets that depend only on ATMKoala headers/runtime (file descriptors, metadata, directory iteration, cwd/access/umask/tty, time and environment) and avoid Linux-only proc/ioctl/socket/fork/exec requirements until the user-process subsystem is complete.
