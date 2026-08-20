#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-new-command-surfaces.mon
TERM=/tmp/atmkoala-new-command-surfaces-terminal.ppm
CUBE=/tmp/atmkoala-new-command-surfaces-cube.ppm
rm -f "$SOCK" "$TERM" "$CUBE" /tmp/atmkoala-new-command-surfaces-terminal.png /tmp/atmkoala-new-command-surfaces-cube.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-new-command-surfaces.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){ local s="$1" c k; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; case "$c" in ' ') k=spc;; *) k="$c";; esac; cmd "sendkey $k"; done; }
sleep 9
send_text swap; cmd 'sendkey ret'; sleep 1
send_text gpu; cmd 'sendkey ret'; sleep 2
cmd "screendump $TERM"
send_text cube; cmd 'sendkey ret'; sleep 2
cmd "screendump $CUBE"
for f in "$TERM" "$CUBE"; do if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$f" > "${f%.ppm}.png"; else ffmpeg -y -loglevel error -i "$f" "${f%.ppm}.png"; fi; done
echo 'new command surfaces regression passed: terminal and cube screenshots created.'
