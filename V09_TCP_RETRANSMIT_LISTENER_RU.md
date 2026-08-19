# ATMKoala v0.9 — TCP retransmit and listener/accept checkpoint

## Реализованный transport scope

This checkpoint expands the previous bounded TCP client with restricted retry behavior and a single-connection passive-open path. The implementation remains experimental and native to ATMKoala; it is not a Linux TCP stack or a production server API.

| Feature | Contract |
|---|---|
| Active SYN retry | `connect` sends SYN up to three times, dividing the supplied timeout across attempts. A connection is established only after a matching SYN-ACK and an emitted ACK. |
| Data retry | A payload of at most 512 bytes is resent up to three times until a matching ACK covers the expected next sequence number. Failure transitions the connection into `ERROR`. |
| Passive ARP | ARP requests addressed to the configured local IPv4 address are observed, cached and answered. This gives an incoming peer a MAC-resolution route before it sends SYN. |
| Passive open | `listen` creates `LISTEN` state. `accept` validates an inbound SYN for its local port, sends SYN-ACK up to three times and returns only after the correct ACK. |
| Listener lifecycle | Listener and accepted child both belong to one task. The child consumes a separate free task-local socket FD and global slot; failed handshakes release it. |
| Backlog | The native ABI accepts only `0` or `1`; there is no connection queue. |

## Native ABI

| Syscall | Number | User API |
|---|---:|---|
| `bind` | `0xA712` | `bind(fd, sockaddr*, len)` |
| `listen` | `0xA713` | `listen(fd, backlog)` |
| `accept` | `0xA714` | `accept(fd, sockaddr*, socklen_t*)` |

`bind` accepts only the local configured IPv4 address or the all-zero wildcard address. `listen` requires a nonzero bound port. `accept` pre-validates a requested peer-address output buffer before it begins the blocking handshake.

## Verification

The full kernel and static libc fixture build completed successfully. A refreshed GRUB ISO booted in QEMU with RTL8139/SLIRP and reached `VBE OK`. This confirms build and boot integration, but does not prove external TCP traffic: the graphical shell is not accessible through the headless serial transport used for this run.

## Remaining safety boundaries

There is no congestion control, receive window tracking, sequence wrap handling, TCP checksum verification, fragmentation/reassembly, RST processing, SYN backlog, nonblocking mode, `poll`/`select`, DNS userspace ABI, multi-peer ARP cache or TLS. The listener is therefore suitable only as a narrow experimental transport foundation. curl and HTTPS remain unsupported.
