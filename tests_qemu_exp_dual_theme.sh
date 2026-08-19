#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.5.iso}
SOCK=/tmp/atmkoala-dual-theme.mon
DARK=/tmp/atmkoala-exp-dark-mono.ppm
WHITE=/tmp/atmkoala-exp-white-paper.ppm
rm -f "$SOCK" "$DARK" "$WHITE" /tmp/atmkoala-exp-dark-mono.png /tmp/atmkoala-exp-white-paper.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" \
  -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-dual-theme.log 2>&1 &
PID=$!
cleanup(){ if kill -0 "$PID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$PID" || true; fi; }
trap cleanup EXIT
sleep 8
# Default desktop must start as Dark Mono. Open Start, select Settings (item 5), then press W.
{ printf 'screendump %s\n' "$DARK"; sleep 1; printf 'sendkey f2\n'; sleep 1; printf 'sendkey 5\n'; sleep 1; printf 'sendkey w\n'; sleep 2; printf 'screendump %s\n' "$WHITE"; } | socat - UNIX-CONNECT:"$SOCK" >/dev/null
ffmpeg -y -loglevel error -i "$DARK" /tmp/atmkoala-exp-dark-mono.png
ffmpeg -y -loglevel error -i "$WHITE" /tmp/atmkoala-exp-white-paper.png
