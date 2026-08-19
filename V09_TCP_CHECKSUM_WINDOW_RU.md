# ATMKoala v0.9 — TCP checksum and window-management checkpoint

## Implemented protections

The bounded native TCP transport now validates received IPv4 and TCP data before a segment can alter connection state, sequence counters or window state.

| Area | Implemented contract |
|---|---|
| IPv4 validation | Ethernet type, IPv4 version, IHL range, total-length bounds and IPv4 header checksum must be valid before TCP parsing. |
| TCP validation | TCP data-offset bounds, IPv4 payload bounds and TCP pseudo-header checksum must be valid before SYN, SYN-ACK, ACK or payload processing. |
| Peer window | The peer advertised receive window is taken only from validated SYN-ACK, SYN or ACK/data headers. A zero peer window blocks send. |
| Send admission | `atm_tcp_send` rejects a payload above the bounded 512-byte transport maximum or above the current peer advertised window. |
| Local window | The stack advertises a bounded 512-byte receive window in control and data packets. Receive rejects data larger than the local window. |
| ACK handling | A matching ACK following a retransmitted send updates the peer window and advances the local sequence number. |

## Regression coverage

`atm_tcp_selftest()` synthesizes a valid Ethernet/IPv4/TCP frame, verifies successful checksum validation, corrupts the IPv4 header and verifies rejection, then verifies window admission at the boundary. The shell command `net test` now reports both ARP-cache and TCP-checksum/window results without requiring a peer.

## Evidence

`make all` succeeded. A refreshed GRUB ISO completed the supported QEMU boot regression with RTL8139/SLIRP and recorded `VBE OK`. The headless environment still cannot inject commands into the Exp graphical shell, so this is build/boot and deterministic self-test coverage, not external traffic proof.

## Remaining limits

The transport still lacks congestion control, delayed ACK, selective acknowledgment, dynamic receive-buffer accounting, out-of-order queueing, fragmentation/reassembly, sequence-number wrap handling, RST semantics, `poll`/`select`, multi-entry ARP/NDP, DNS userspace ABI and TLS. It therefore remains an experimental native TCP foundation; curl and HTTPS remain unsupported.
