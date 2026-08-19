#!/bin/sh
set -eu
WORK=/tmp/atmkoala-ext2-test
IMG="$WORK/ext2-regression.img"
FS="$WORK/test.ext2"
DATA="$WORK/indirect.bin"
rm -rf "$WORK"
mkdir -p "$WORK"
# 6 MiB file: direct, single-indirect and double-indirect reference locations.
dd if=/dev/zero of="$DATA" bs=1M count=6 status=none
printf 'DIRECT-OK\n' | dd of="$DATA" bs=1 seek=0 conv=notrunc status=none
printf 'SINGLE-INDIRECT-OK\n' | dd of="$DATA" bs=1 seek=$((12*4096)) conv=notrunc status=none
printf 'DOUBLE-INDIRECT-OK\n' | dd of="$DATA" bs=1 seek=$(((12+1024)*4096)) conv=notrunc status=none
mkfs.ext2 -q -F -b 4096 -L ATM-EXT2-TEST "$FS" 8192
debugfs -w -R "write $DATA /indirect.bin" "$FS" >/dev/null 2>&1
debugfs -w -R "symlink /marker-link /indirect.bin" "$FS" >/dev/null 2>&1
# Disk: 1 MiB MBR gap then a 32 MiB Linux/EXT2 primary partition.
dd if=/dev/zero of="$IMG" bs=1M count=34 status=none
printf '\000\000\000\000\203\000\000\000\000\010\000\000\000\000\001\000' | dd of="$IMG" bs=1 seek=446 conv=notrunc status=none
printf '\125\252' | dd of="$IMG" bs=1 seek=510 conv=notrunc status=none
dd if="$FS" of="$IMG" bs=1M seek=1 conv=notrunc status=none
echo "$IMG"
