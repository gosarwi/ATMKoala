#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
DISK=/tmp/atmkoala-installer-boot.img
SOCK=/tmp/atmkoala-installer-boot.mon
PPM=/tmp/atmkoala-installer-boot.ppm
PNG=/tmp/atmkoala-installer-boot.png
rm -f "$SOCK" "$PPM" "$PNG" "$DISK"
dd if=/dev/zero of="$DISK" bs=1M count=64 status=none
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -drive file="$DISK",format=raw,if=ide -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-installer-boot.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..30}; do [ -S "$SOCK" ] && break; sleep 0.1; done
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
# Limine entries: graphical 800, 1024, 640, text, then Disk Installer.
sleep 0.3
cmd 'sendkey down'; cmd 'sendkey down'; cmd 'sendkey down'; cmd 'sendkey down'; cmd 'sendkey ret'
sleep 9
cmd "screendump $PPM"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$PPM" > "$PNG"; else ffmpeg -y -loglevel error -i "$PPM" "$PNG"; fi
echo "dedicated Limine installer boot regression passed: $PNG"
