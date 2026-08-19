# ATMKoala v0.9 — native user socket ABI checkpoint

## Реализованный scope

ATMKoala now provides a narrow **native**, task-local TCP client socket surface. It is not a Linux socket ABI implementation and is intentionally insufficient for upstream curl.

| Layer | Implemented behavior |
|---|---|
| Kernel registry | 16 bounded global slots; each slot is owned by exactly one `task_t`. |
| User descriptor namespace | Socket FDs occupy free task-local slots 3–31 and do not alias VFS backend FDs. |
| Creation contract | Only `AF_INET`, `SOCK_STREAM`, `IPPROTO_TCP` is accepted. |
| Connection contract | `connect()` accepts exactly the fixed 16-byte IPv4 `sockaddr_in` form, copies it through checked user-access logic and uses bounded TCP connect timeout. |
| I/O contract | Existing native `read`, `write` and `close` dispatch by descriptor kind; TCP user payload is capped at 512 bytes per call by the current bounded transport. |
| Cleanup | `close()` and task cleanup release socket registry state; socket `lseek` and `fstat` return failure rather than pretending sockets are VFS files. |
| libc surface | `sys/socket.h`, `netinet/in.h`, `socket()` and `connect()` wrappers are part of the freestanding static libc. |

## Syscalls

| Number | Call | Notes |
|---|---|---|
| `0xA710` | `socket(domain,type,protocol)` | Native ATM ABI; user-mode only. |
| `0xA711` | `connect(fd,addr,addrlen)` | Native ATM ABI; `addrlen` must be `sizeof(struct sockaddr_in)`. |
| `0`, `1`, `3` | `read`, `write`, `close` | Existing calls also operate on a recognized socket FD. |

## Verification

The kernel syscall self-test now creates/closes a user-mode TCP socket descriptor and validates registry lifecycle. The embedded static CPL3 libc smoke executable also creates/closes a TCP socket. `make all` succeeded and the new ISO reached `VBE OK` in QEMU with RTL8139 plus SLIRP.

## Explicit limitations

The present TCP client lacks retransmission, TCP checksum validation, congestion/window management, fragmentation/reassembly, poll/select, nonblocking mode, DNS user ABI, listeners/accept and concurrent connection safety. Therefore sockets are an experimental native client facility, not a production network stack. Upstream curl remains audit-only; HTTP can be considered only after live socket traffic tests, and HTTPS requires a TLS backend plus certificate validation.
