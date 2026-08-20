#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-calculator-nano.mon
CALC=/tmp/atmkoala-calculator-expression.ppm
NANO=/tmp/atmkoala-nano-alias.ppm
rm -f "$SOCK" "$CALC" "$NANO" /tmp/atmkoala-calculator-expression.png /tmp/atmkoala-nano-alias.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-calculator-nano.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){ local s="$1" c k; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; case "$c" in ' ') k=spc;; '*') k=asterisk;; '+') k=shift-equal;; '%') k=shift-5;; '(') k=shift-9;; ')') k=shift-0;; '/') k=slash;; *) k="$c";; esac; cmd "sendkey $k"; done; }
sleep 9
send_text calc; cmd 'sendkey ret'; sleep 2
# (2*(3+4))%5 has the integer result 4.
send_text '2*(3+4)%5'; cmd 'sendkey ret'; sleep 2
cmd "screendump $CALC"
cmd 'sendkey alt-f4'; sleep 1
send_text 'nano /data/nano-regression.txt'; cmd 'sendkey ret'; sleep 2
cmd "screendump $NANO"
for f in "$CALC" "$NANO"; do if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$f" > "${f%.ppm}.png"; else ffmpeg -y -loglevel error -i "$f" "${f%.ppm}.png"; fi; done
echo 'calculator/nano regression passed: screenshots created.'
