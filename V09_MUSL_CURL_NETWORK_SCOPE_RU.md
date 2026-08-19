# v0.9 musl, curl and network scope

## Decision

ATMKoala cannot directly build or run upstream musl as its libc: musl is built on the Linux syscall layer, whereas ATMKoala uses a distinct `int $0x80` ABI with a deliberately small syscall surface. The correct goal is an **ATM-native musl-compatible libc subset**: compatible standard C/POSIX headers and semantics implemented over ATM syscalls, with clearly separate ATM CRT and linker script.

## curl

curl has a permissive curl license, so source adaptation is legally possible provided copyright and permission notices are retained. It still needs sockets, DNS, poll/select, clock/time, file APIs and a TLS backend for a secure HTTPS client. The first realistic milestone is an ATM-native **HTTP/1.1 over TCP** client after sockets/DNS are implemented; HTTPS must remain unavailable until a reviewed TLS backend and certificate policy exist.

## Existing platform state

| Area | Existing source signal | Safe next action |
|---|---|---|
| ext2 | Read path plus guarded direct-block overwrite; no allocation/truncate/journal support | Build test image and add mount/read/write regression before extending metadata writes. |
| disk partitioning | `partmgr`, `diskmgr` and installer modules exist | Add cfdisk-style inspect/edit UI only with explicit write confirmation and MBR validation. |
| terminal/Notepad | `fish_shell`, terminal routines and `notepad` shell/GUI references exist | Audit interaction/rendering first; preserve terminal clipping and cursor invariants. |
| NIC/network | `net`, `dns`, `icmp` modules exist | Validate NIC driver/protocol state, then add transport socket ABI rather than claiming curl support. |
| libc | static CRT, allocator, strings and basic unistd wrappers exist | Add `fstat`, `lseek`, stdio-lite and directory interfaces incrementally. |

## References

[1]: https://musl.libc.org/about.html "musl overview"
[2]: https://wiki.musl-libc.org/supported-platforms "musl supported platforms"
[3]: https://curl.se/docs/copyright.html "curl license"
