#!/usr/bin/env python3
"""Create a disposable MBR+Btrfs inspection image.
No Btrfs tree is created: the image exercises only ATMKoala's read-only
superblock/mirror/CRC32C inspector."""
from pathlib import Path
import struct

WORK = Path('/tmp/atmkoala-btrfs-test')
IMG = WORK / 'btrfs-mirror-inspect.img'
SECTOR = 512
PART_LBA = 2048
SIZE = 80 * 1024 * 1024
PRIMARY = 0x10000
MIRROR1 = 0x4000000


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc & 0xFFFFFFFF


def superblock(offset: int, generation: int, label: bytes) -> bytes:
    sb = bytearray(4096)
    sb[0x20:0x30] = bytes.fromhex('112233445566778899aabbccddeeff00')
    struct.pack_into('<Q', sb, 0x30, offset)
    sb[0x40:0x48] = b'_BHRfS_M'
    struct.pack_into('<Q', sb, 0x48, generation)
    struct.pack_into('<Q', sb, 0x70, 1024 * 1024 * 1024)
    struct.pack_into('<Q', sb, 0x78, 256 * 1024 * 1024)
    struct.pack_into('<Q', sb, 0x88, 1)
    struct.pack_into('<I', sb, 0x90, 4096)
    struct.pack_into('<I', sb, 0x94, 16384)
    struct.pack_into('<H', sb, 0xC4, 0)  # CRC32C
    sb[0x12B:0x12B + len(label)] = label
    struct.pack_into('<I', sb, 0, crc32c(sb[0x20:]))
    return bytes(sb)

WORK.mkdir(parents=True, exist_ok=True)
image = bytearray(SIZE)
part_sectors = (SIZE // SECTOR) - PART_LBA
image[446:462] = bytes([0, 0, 0, 0, 0x83, 0, 0, 0]) + struct.pack('<II', PART_LBA, part_sectors)
image[510:512] = b'\x55\xaa'
base = PART_LBA * SECTOR
image[base + PRIMARY:base + PRIMARY + 4096] = superblock(PRIMARY, 42, b'ATMKOALA-BTRFS-PRIMARY')
image[base + MIRROR1:base + MIRROR1 + 4096] = superblock(MIRROR1, 43, b'ATMKOALA-BTRFS-MIRROR')
IMG.write_bytes(image)
print(IMG)
