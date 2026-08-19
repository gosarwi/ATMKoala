# curl vendor provenance for ATMKoala

| Field | Value |
|---|---|
| Upstream | `https://github.com/curl/curl` |
| Pinned commit | `695aa15743685a9d46c4c41bb9c95221d4659541` |
| Upstream describe | `rc-8_22_0-2-14-g695aa15743` |
| Retrieved | 2026-08-19 |
| License | curl license; retain `COPYING` and notices |

## ATMKoala status

This is an unmodified source snapshot retained for compatibility auditing. It is **not** built or shipped as an ATMKoala executable. Current ATMKoala has a bounded kernel-internal TCP client but no task-local socket descriptors, polling API, complete TCP implementation, DNS resolver ABI, TLS backend, certificate validation or standard hosted libc. The first possible adaptation target is a narrow HTTP/1.1 client built over a reviewed ATM-native socket layer; it must not claim HTTPS support until TLS is implemented and verified.

## Required attribution

Any distributed source adaptation must preserve upstream copyright and permission notices, specifically the upstream `COPYING` material.
