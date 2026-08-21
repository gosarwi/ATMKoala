#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-mouse-snake.mon
MOUSE=/tmp/atmkoala-mouse-diagnostic.ppm
SNAKE_A=/tmp/atmkoala-snake-a.ppm
SNAKE_B=/tmp/atmkoala-snake-b.ppm
rm -f "$SOCK" "$MOUSE" "$SNAKE_A" "$SNAKE_B" /tmp/atmkoala-mouse-diagnostic.png /tmp/atmkoala-snake-a.png /tmp/atmkoala-snake-b.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-mouse-snake.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
[ -S "$SOCK" ] || { echo 'QEMU monitor did not become ready' >&2; exit 1; }
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){ local s="$1" c k; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; case "$c" in ' ') k=spc;; *) k="$c";; esac; cmd "sendkey $k"; done; }
sleep 9
# HMP delivers a relative PS/2 movement; the terminal command preserves the
# packet and controller counters in a visual capture for manual field review.
cmd 'mouse_move 48 24'
sleep 1
send_text mouse; cmd 'sendkey ret'; sleep 2
cmd "screendump $MOUSE"
# Launcher index 13 is native Snake: F2, thirteen down arrows, Enter.
cmd 'sendkey f2'; sleep 1
for _ in {1..13}; do cmd 'sendkey down'; done
cmd 'sendkey ret'; sleep 1
cmd "screendump $SNAKE_A"
sleep 1
cmd "screendump $SNAKE_B"
cmp -s "$SNAKE_A" "$SNAKE_B" && { echo 'Snake frames are identical; expected timed progression' >&2; exit 1; }
for f in "$MOUSE" "$SNAKE_A" "$SNAKE_B"; do if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$f" > "${f%.ppm}.png"; else ffmpeg -y -loglevel error -i "$f" "${f%.ppm}.png"; fi; done
echo 'mouse/Snake regression passed: mouse diagnostics and changing Snake frames captured.'
