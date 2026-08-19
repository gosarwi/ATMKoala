# BusyBox для ATMKoala v0.9: реализуемость и границы

## Вывод

**Да, BusyBox-подобный userspace для ATMKoala реализуем.** Однако существует два разных пути, которые нельзя смешивать.

| Вариант | Статус для ATMKoala | Причина |
|---|---|---|
| Запустить готовый Linux BusyBox ELF | Пока невозможно. | Нужны Linux syscalls, Linux ABI, полноценная libc, dynamic/runtime semantics, которые ATMKoala намеренно ещё не заявляет. |
| Портировать upstream BusyBox source | Возможен позднее, но GPLv2. | При распространении бинарника потребуются complete corresponding source, configuration и patch set. [2] |
| Собственный ATMBox multicall runtime | Начинаем сейчас. | Сохраняет MIT-native направление, использует ATM static libc and native ABI, и даёт знакомый BusyBox command model. |

## Что заимствуем как идею

BusyBox описывает multicall binary: один executable dispatches applets either through `busybox <applet>` or through an applet-named link. Shared utility code reduces the combined footprint.[1] Эта архитектура не является лицензируемой уникальной частью кода; ATMBox реализует её independently with our own source and ABI.

> ATMBox is **not** a BusyBox fork, is not Linux BusyBox compatible, and must not copy GPLv2 BusyBox source into MIT ATMKoala code.

## Initial ATMBox profile

Первый native static ELF will use command form `atmbox <applet> [args...]`. Applet names will be dispatchable without symbolic links at first, because the current VFS/exec user process and applet installation model is still developing.

| First applet | Required current capability | Value |
|---|---|---|
| `true`, `false`, `echo`, `printf` | CRT, stdout, strings | Runtime and shell scripting baseline. |
| `basename`, `dirname`, `pwd` | strings, task-local cwd | Portable path primitives. |
| `cat`, `head`, `wc` | open/read/write | First VFS streaming proof. |
| `mkdir`, `touch`, `rm` | POSIX VFS mutation wrappers | Add only after these libc APIs are present and tested. |
| `uname`, `id`, `uptime` | Native kernel info ABI | Deferred until a small info syscall/table is exposed. |

## Deferred applet families

Networking, init, login, mount, module management, `ash`, `vi`, `tar`, compression, `sed`/`awk`, device administration and Linux-specific `/proc` semantics remain deferred. They require process spawning/exec, pipes, signals, directory traversal, richer filesystem contracts, sockets or privileged resource controls.

## Frozen bootloader boundary

The work tree now treats `boot/atmboot` and `boot/atmuefi` as frozen verified components. No ATMBox, POSIX/libc or GUI work will modify those directories or their image formats. Main-branch work targets `src/`, `sdk/` and userspace fixtures only.

## References

[1] [BusyBox: The Swiss Army Knife of Embedded Linux](https://busybox.net/BusyBox.html)

[2] [BusyBox GPLv2 license and distribution obligations](https://busybox.net/license.html)
