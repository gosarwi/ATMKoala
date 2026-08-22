#!/usr/bin/env bash
# Guarded Ext2 direct-block write regression.  This script deliberately does
# not test allocation, truncate, directory mutation, indirect blocks or journal
# handling: the kernel does not claim those operations.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ISO=${1:-"$ROOT/atmkoala-OS-v0.9-limine.iso"}
SOCK=/tmp/atmkoala-ext2-write.mon
SERIAL=/tmp/atmkoala-ext2-write.serial
CAPTURE=/tmp/atmkoala-ext2-write.ppm
IMG=$($ROOT/tests_make_ext2_disk.sh)
FS_AFTER=/tmp/atmkoala-ext2-test/after.ext2
OUT=/tmp/atmkoala-ext2-test/indirect-after.bin
rm -f "$SOCK" "$SERIAL" "$CAPTURE" "$FS_AFTER" "$OUT"
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" \
  -drive "file=$IMG,format=raw,if=ide" \
  -monitor "unix:$SOCK,server,nowait" -serial "file:$SERIAL" -display none >/tmp/atmkoala-ext2-write.log 2>&1 &
PID=$!
cleanup(){
  if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi
}
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
[ -S "$SOCK" ] || { echo 'Ext2 QEMU monitor did not start' >&2; exit 1; }
key_for(){ case "$1" in ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;; [A-Z]) printf 'shift-%s' "${1,,}";; *) printf '%s' "$1";; esac; }
send_line(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; printf 'sendkey %s\n' "$(key_for "$c")"; done; printf 'sendkey ret\n'; }
sleep 9
{
  send_line 'ext2 mount 0 0'; sleep 1
  send_line 'ext2 rw on'; sleep 1
  send_line 'ext2 write /indirect.bin 0 DIRECT-WRITE-OK'; sleep 1
  send_line 'ext2 rw off'; sleep 1
  printf 'screendump %s\n' "$CAPTURE"
  printf 'quit\n'
} | socat - UNIX-CONNECT:"$SOCK" >/dev/null
wait "$PID" || true
# The test image has its Ext2 primary partition at exactly 1 MiB.
dd if="$IMG" of="$FS_AFTER" bs=1M skip=1 count=32 status=none
debugfs -R "dump -p /indirect.bin $OUT" "$FS_AFTER" >/dev/null 2>&1
[ "$(head -c 15 "$OUT")" = 'DIRECT-WRITE-OK' ] || { echo 'Ext2 guarded direct write did not persist expected bytes' >&2; exit 1; }
echo 'Ext2 guarded direct-block write regression passed (no allocation/truncate/indirect metadata claim).'
