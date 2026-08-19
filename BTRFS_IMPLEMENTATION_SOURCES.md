# Btrfs implementation sources

## Design constraints for ATMKoala Btrfs work

The official Btrfs design documentation states that metadata and data are protected by copy-on-write; a committed transaction redirects pointers to newly allocated blocks, and the root tree/superblock update establishes the new committed state. It also states that tree blocks include checksums, filesystem UUID, bytenr, generation and owner; generation is used to detect misplaced writes.

The on-disk format documentation states that Btrfs uses logical addresses resolved to physical addresses through the chunk tree, with system chunk bootstrap data stored in the superblock. It documents the three superblock mirror offsets (64 KiB, 64 MiB, 256 GiB), superblock CRC32C, root/chunk-root logical pointers, feature flags and the role of extent, root, chunk and checksum trees.

## Sources

1. Btrfs design: https://btrfs.readthedocs.io/en/latest/dev/dev-btrfs-design.html
2. Btrfs on-disk format: https://btrfs.readthedocs.io/en/latest/dev/On-disk-format.html
3. Btrfs developer documentation index: https://github.com/btrfs/btrfs-dev-docs

## Validation findings

A disposable 192 MiB MBR-partitioned Btrfs image made by `mkfs.btrfs` confirmed that ATMKoala's original CRC32C routine was incomplete: the stored superblock digest equals reflected CRC32C with `seed=0xffffffff` and a final XOR of `0xffffffff`. The kernel implementation was corrected accordingly. The real image then passed probe and CRC32C validation for its 64 KiB and 64 MiB mirrors. The initial narrow label-transaction test still rejected its commit, so the implementation now reports preflight, write, and post-write verification stages separately for the next QEMU regression.

The stage diagnostics showed that the real-image label transaction reaches the ATA PIO write path but fails on a mirror sector with last ATA status `0xC0` and error register `0x00`, indicating a busy device rather than a Btrfs checksum mismatch. The existing driver flushes after every 512-byte sector. The next correction is to flush once after a `disk_write` multi-sector batch, avoiding immediate chained FLUSH commands while the emulated ATA device is busy.

After increasing the bounded PIO completion timeout and issuing one cache flush per multi-sector batch, the QEMU regression successfully committed `btrfs label atmkoala-label-tx`. ATMKoala re-read both available mirrors as CRC32C-valid generation 7, while `btrfs inspect-internal dump-super` on the detached host-side partition independently reported label `atmkoala-label-tx` and generation 7. This validates the implemented scope: checksum-protected, mirrored superblock-label metadata update. It is not validation of B-tree, file data, extent allocation, checksum-tree, mount or general-write support.
