#!/usr/bin/env python3
"""Create a disposable MBR-partitioned real Btrfs image for ATMKoala tests."""
from pathlib import Path
import os

img = Path("/tmp/atmkoala-real-btrfs.img")
size = 192 * 1024 * 1024
start_lba = 2048
sectors = size // 512 - start_lba

with img.open("wb") as f:
    f.truncate(size)
    mbr = bytearray(512)
    off = 446
    mbr[off] = 0x00
    mbr[off + 4] = 0x83
    mbr[off + 8:off + 12] = start_lba.to_bytes(4, "little")
    mbr[off + 12:off + 16] = sectors.to_bytes(4, "little")
    mbr[510:512] = b"\x55\xaa"
    f.seek(0)
    f.write(mbr)

print(f"{img} start_lba={start_lba} sectors={sectors}")
