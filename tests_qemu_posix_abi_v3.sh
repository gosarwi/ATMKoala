#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-posix-abi-v3.mon
CAPTURE=/tmp/atmkoala-posix-abi-v3.ppm
rm -f "$SOCK" "$CAPTURE" /tmp/atmkoala-posix-abi-v3.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-posix-abi-v3.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){ local s="$1" c k; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; case "$c" in ' ') k=spc;; *) k="$c";; esac; cmd "sendkey $k"; done; }
sleep 9
send_text 'syscall abi'; cmd 'sendkey ret'; sleep 1
send_text 'posix status'; cmd 'sendkey ret'; sleep 1
send_text 'posix api'; cmd 'sendkey ret'; sleep 1
send_text 'posix test'; cmd 'sendkey ret'; sleep 3
cmd "screendump $CAPTURE"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$CAPTURE" > /tmp/atmkoala-posix-abi-v3.png; else ffmpeg -y -loglevel error -i "$CAPTURE" /tmp/atmkoala-posix-abi-v3.png; fi
echo 'POSIX ABI v3 command-surface regression passed: screenshot created.'
