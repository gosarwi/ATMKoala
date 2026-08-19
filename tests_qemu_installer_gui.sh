#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.5-installer-test.iso}
DISK=${2:-/tmp/atmkoala-installer-target.img}
SOCK=/tmp/atmkoala-installer.mon
PPM=/tmp/atmkoala-installer.ppm
PNG=/tmp/atmkoala-installer.png
rm -f "$SOCK" "$PPM" "$PNG" "$DISK"
dd if=/dev/zero of="$DISK" bs=1M count=64 status=none
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -drive file="$DISK",format=raw,if=ide \
  -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-installer.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
sleep 9
{ printf 'sendkey ret\n'; sleep 1; printf 'sendkey ret\n'; sleep 1; printf 'sendkey i\n'; sleep 4; printf 'screendump %s\n' "$PPM"; } | socat - UNIX-CONNECT:"$SOCK" >/dev/null
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$PPM" > "$PNG"; else ffmpeg -y -loglevel error -i "$PPM" "$PNG"; fi
# CatFS magic 0xCAFE4002 in little endian at LBA0.
[ "$(od -An -tx4 -N4 "$DISK" | tr -d ' ')" = "cafe4002" ]
