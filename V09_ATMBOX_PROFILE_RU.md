# ATMBox v0.9 — минимальный native multicall профиль

## Формат

ATMBox — собственный статический `ET_EXEC` userspace executable. Его canonical interface is:

```text
atmbox <applet> [arguments...]
```

Как у BusyBox, dispatcher chooses an applet by name and shares the CRT, allocator, parser, error handling and I/O helpers. На текущем этапе это **ATMBox**, а не upstream BusyBox and not a Linux binary.

## First applet set

| Applet | v0.9 scope | Required interfaces |
|---|---|---|
| `true`, `false` | Correct exit status. | `_exit`/CRT. |
| `echo` | `-n` plus positional words. | `write`, strings. |
| `printf` | `%s`, `%d`, `%%`; no locale/float. | `write`, integer conversion. |
| `basename`, `dirname` | Lexical POSIX-like path trimming. | strings only. |
| `cat` | Stream each file or stdin; bounded 512-byte buffer. | `open`, `read`, `write`, `close`, errno. |
| `wc` | Count bytes, lines, words for one file/stdin. | Stream I/O, integer conversion. |
| `--list`, `--help`, `--selftest` | Runtime discovery and deterministic regression. | Dispatcher and stdout. |

## Intentional exclusions

`sh`, `ash`, `init`, `mount`, `login`, networking, `tar`, compression, `sed`, `awk`, package management, users and device management do not belong to the first ATMBox. They need either `execve` argv construction, pipes, directory iteration, process groups, sockets, privilege boundaries or complete filesystem semantics.

## Current launcher constraint

The native ELF loader currently starts a process with `argc=0`, `argv=NULL` and `envp=NULL`. Consequently, the first embedded ATMBox fixture can verify dispatcher/appet code through `--selftest`, but the shell cannot yet pass a user-typed applet name into a native CPL 3 process. A future `execve`/spawn ABI must construct an immutable argv/envp stack before `atmbox <applet>` becomes a normal external command.

This is not a reason to delay the runtime: a tested multicall dispatcher, applet ABI and I/O contract can be completed now, while exec argument marshalling becomes the next kernel/userspace bridge.
