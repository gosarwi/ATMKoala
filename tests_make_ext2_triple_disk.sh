#!/bin/sh
set -eu
WORK=/tmp/atmkoala-ext2-triple
IMG="$WORK/ext2-triple.img"
FS="$WORK/triple.ext2"
DATA="$WORK/triple.bin"
rm -rf "$WORK"
mkdir -p "$WORK"
# 1 KiB blocks: triple-indirect starts after 12 direct + 256 single + 256^2 double blocks.
TRIPLE_OFF=$(((12+256+65536)*1024))
dd if=/dev/zero of="$DATA" bs=1M count=66 status=none
printf 'TRIPLE-INDIRECT-OK\n' | dd of="$DATA" bs=1 seek="$TRIPLE_OFF" conv=notrunc status=none
mkfs.ext2 -q -F -b 1024 -L ATM-EXT2-TRIPLE "$FS" 131072
debugfs -w -R "write $DATA /triple.bin" "$FS" >/dev/null 2>&1
dd if=/dev/zero of="$IMG" bs=1M count=130 status=none
# Primary Linux partition: LBA 2048, 128 MiB (262144 sectors).
printf '\000\000\000\000\203\000\000\000\000\010\000\000\000\000\004\000' | dd of="$IMG" bs=1 seek=446 conv=notrunc status=none
printf '\125\252' | dd of="$IMG" bs=1 seek=510 conv=notrunc status=none
dd if="$FS" of="$IMG" bs=1M seek=1 conv=notrunc status=none
echo "$IMG"
