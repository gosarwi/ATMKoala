#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.5.iso}
SOCK=/tmp/atmkoala-exp-ui.mon
PPM=/tmp/atmkoala-exp-icons-tray.ppm
PNG=/tmp/atmkoala-exp-icons-tray.png
rm -f "$SOCK" "$PPM" "$PNG"
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" \
  -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-exp-ui.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
sleep 9
printf 'sendkey f2\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null
sleep 2
printf 'screendump %s\n' "$PPM" | socat - UNIX-CONNECT:"$SOCK" >/dev/null
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$PPM" > "$PNG"; else ffmpeg -y -loglevel error -i "$PPM" "$PNG"; fi
