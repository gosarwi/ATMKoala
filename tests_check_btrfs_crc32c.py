#!/usr/bin/env python3
"""Compare CRC32C variants against a real Btrfs superblock."""
from pathlib import Path

p = Path("/tmp/atmkoala-real-btrfs.part")
sb = p.read_bytes()[0x10000:0x10000 + 4096]
stored = int.from_bytes(sb[:4], "little")

def crc32c(data: bytes, seed: int, final_xor: bool) -> int:
    c = seed
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0x82F63B78 if c & 1 else 0)
    return (c ^ 0xffffffff) if final_xor else c

print(f"stored=0x{stored:08x}")
for seed in (0, 0xffffffff):
    for final in (False, True):
        print(f"seed=0x{seed:08x} final_xor={final}: 0x{crc32c(sb[0x20:], seed, final):08x}")
