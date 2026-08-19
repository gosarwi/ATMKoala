# Toybox vendor provenance for ATMKoala

| Field | Value |
|---|---|
| Upstream | `https://github.com/landley/toybox` |
| Pinned commit | `b7ec52ac35e075caffca5d330995d44e8dbfc8c3` |
| Upstream version | `0.8.14` |
| Retrieval date | 2026-08-19 |
| Upstream license | Toybox `LICENSE`: permissive ISC/0BSD-style license |
| Target | ATMKoala main kernel/userspace branch only |

## Scope

The unmodified source tree is retained for audit and selective adaptation. `ATMBOOT` and `ATMUEFI` are out of scope. ATMKoala must not represent an unmodified upstream Linux build as a working native executable: upstream Toybox assumes a hosted C library and Linux/Android/BSD facilities, while ATMKoala v0.9 currently exposes only a bounded native POSIX subset.

## First adaptation boundary

The first target is a static `ET_EXEC` fixture with Toybox-style multicall dispatch and the `true`, `false`, `echo`, `printf`, `cat`, and `wc` applet behaviour. It will use the existing ATM static CRT and syscall ABI rather than Linux syscall numbers. The source selection and every compatibility shim must be documented before copying or modifying any upstream file.

## Upstream notices

Retain the upstream `LICENSE` file and copyright notices in every distributed adaptation and provide this provenance file in source releases.

## References

[1]: https://github.com/landley/toybox/blob/master/LICENSE "Toybox LICENSE"
[2]: https://landley.net/toybox/ "Toybox overview"
