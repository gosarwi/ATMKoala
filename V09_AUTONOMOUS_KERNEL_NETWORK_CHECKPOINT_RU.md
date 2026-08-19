# ATMKoala v0.9 — автономный checkpoint kernel/network/userspace

## Реализовано в текущем checkpoint

| Area | Implemented and built |
|---|---|
| RTL8139 TX | Safe zero padding of short Ethernet frames; no read beyond caller buffer. |
| RTL8139 RX | Hardware status/size checks, FCS removal, 8 KiB ring-pointer semantics with tail slack. |
| ARP | Single bounded cache, validated reply observation, 6000-tick expiry, active request/wait before TCP connect. |
| UDP control parsers | DHCP option-length bound and DNS IPv4/IHL/question-section bounds. |
| TCP internal client | Kernel-internal ARP → SYN/SYN-ACK/ACK setup; capped 512-byte ACK+PSH transmit; matched in-order receive plus ACK; FIN+ACK close. |
| MBR/cfdisk backend | Status/range/overflow/overlap checks, physical ATA-capacity validation at write commit. |
| ext2 | Inode record geometry guard and filesystem-block-range-to-MBR-partition guard. |
| POSIX/native libc | Non-destructive `read` destination preflight, `lseek`, `fstat`, public `sys/stat.h`, CPL3 smoke fstat assertion. |

## Deliberately not claimed

The TCP module is not yet exposed as a user syscall or task-local socket descriptor. It has no TCP checksum verification, retransmission, congestion control, fragmentation/reassembly, window updates, FIN state acknowledgement or listener/server support. It is therefore unsuitable for curl or general networking userspace until a separate socket-lifetime and QEMU traffic regression milestone is complete.

The ext2 driver is not full ext2 write support: it remains limited to guarded in-place direct-block data overwrites on clean non-journalled non-extent volumes. It cannot yet allocate blocks/inodes, grow/truncate files, create directories, update bitmaps or safely operate journalled/ext4 filesystems.

## Validation available

Every source change in this checkpoint was compiled through `make all`. A fresh ISO was built and booted under headless QEMU with `-netdev user` plus `-device rtl8139`; serial log reached VBE `OK`. The graphical Exp shell is not mapped to serial input in that headless configuration, so interactive `posix test` requires the existing display/keyboard QEMU regression route.

## Upstream audit snapshots

| Component | Local path | Pinned revision | Integration status |
|---|---|---|---|
| curl | `third_party/curl/` | `695aa15743685a9d46c4c41bb9c95221d4659541` | Audit-only; no build or binary port. |
| musl | `third_party/musl/` | `f21a96538f78fa8e2040831b4209b35f2fb581da` | Audit-only; ATM libc remains a native static compatibility subset. |

Each vendor tree includes an `ATMKOALA_VENDOR.md` with license/provenance requirements and explicit unsupported-boundary statements.

## Additional continuation fixes

The Disk Manager/cfdisk creation screen now checks proposed ranges as `start >= total || sectors > total - start`; it no longer evaluates potentially overflowing `start + sectors`. The final `mbr_write` commit remains protected by structural and ATA-capacity validation.

A deterministic `net test` command was added. It synthesizes a valid ARP reply, verifies cache insertion and MAC retrieval, and verifies rejection of a mismatched lookup without requiring a live NIC or QEMU network path.

The ext2 mount path now invokes `mbr_validate_drive()` before reading the superblock, so a structurally valid but device-out-of-range MBR entry is rejected before the partition LBA is used.

A `cfdisk test` command now exercises deterministic MBR validation coverage: valid table acceptance, overlap detection, arithmetic-overflow rejection and malformed empty-entry rejection. It performs no disk reads or writes.

## Final build and QEMU evidence

A final `make all` completed successfully, followed by `make iso`. The refreshed `atmkoala-OS-v0.5.iso` was booted for 12 seconds under QEMU with `-netdev user,id=net0 -device rtl8139,netdev=net0`; the serial log recorded VBE discovery and `VBE OK`, with QEMU only ending because of the deliberate timeout. This confirms the updated ISO reaches stable graphical initialization with the supported virtual NIC attached. It does not constitute proof of TCP/curl traffic, because the graphical input shell is not serial-controlled in this headless test configuration.
