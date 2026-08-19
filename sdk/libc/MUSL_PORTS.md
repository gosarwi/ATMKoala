# musl-derived components in ATMKoala static libc

This directory contains a **freestanding, static, ATM ABI adaptation**, not a full build of musl and not a Linux binary-compatibility layer.

## Source provenance

The implementation patterns and selected routines are adapted from the vendored snapshot in `third_party/musl/`, upstream musl `v1.2.6-20-gf21a9653`. musl is MIT licensed. The source-level attribution is retained in every adapted implementation file.

| ATMKoala component | musl source family | Included scope |
|---|---|---|
| `src/ctype.c` | `src/ctype/` | ASCII classification and case mapping; no locale tables or wide-character layer. |
| `src/string.c` | `src/string/` | Memory/string search, spans, tokenization, ASCII case compare and string duplication. |
| `src/stdlib.c` | `src/stdlib/bsearch.c`, `strtol.c` family | Integer conversion, overflow reporting through `errno`, `bsearch`, absolute and division helpers. |

## Explicit non-goals

This stage does not import musl's dynamic loader, Linux syscall layer, pthread/TLS runtime, DNS resolver, locale database, iconv, wide-character subsystem, `stdio` buffering redesign, `qsort` smoothsort, floating-point conversion, or any Linux ABI assumptions. Those components require separate kernel ABI, threading, time, filesystem, or locale work.

The current public interface stays statically linked through `sdk/atm_native_abi.h` and ATMKoala `int $0x80` syscalls. No glibc code or glibc ABI is introduced.
