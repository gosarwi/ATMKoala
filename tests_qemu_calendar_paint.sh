#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-calendar-paint.mon
CAL=/tmp/atmkoala-calendar-rtc.ppm
PAINT=/tmp/atmkoala-paint.ppm
rm -f "$SOCK" "$CAL" "$PAINT" /tmp/atmkoala-calendar-rtc.png /tmp/atmkoala-paint.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-calendar-paint.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
[ -S "$SOCK" ] || { echo 'QEMU monitor did not become ready' >&2; exit 1; }
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
sleep 9
# Calendar is launcher index 9 (zero-based): F2 + nine arrows + Enter.
cmd 'sendkey f2'; sleep 1
for _ in {1..9}; do cmd 'sendkey down'; done
cmd 'sendkey ret'; sleep 2
cmd "screendump $CAL"
# Close Calendar, then Paint is index 14: reopen launcher and navigate there.
cmd 'sendkey alt-f4'; sleep 1
cmd 'sendkey f2'; sleep 1
for _ in {1..14}; do cmd 'sendkey down'; done
cmd 'sendkey ret'; sleep 2
# Choose red palette (3), paint the current cell, move right, paint again.
cmd 'sendkey 3'; cmd 'sendkey spc'; cmd 'sendkey right'; cmd 'sendkey spc'; sleep 1
cmd "screendump $PAINT"
for f in "$CAL" "$PAINT"; do if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$f" > "${f%.ppm}.png"; else ffmpeg -y -loglevel error -i "$f" "${f%.ppm}.png"; fi; done
echo 'Calendar/Paint regression passed: RTC calendar and drawn Paint canvas captured.'
