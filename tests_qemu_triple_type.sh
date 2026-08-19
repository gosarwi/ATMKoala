#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
key_for(){ case "$1" in ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;; *) printf '%s' "$1";; esac; }
send_line(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; printf 'sendkey %s\n' "$(key_for "$c")"; done; printf 'sendkey ret\n'; }
sleep 8
{ send_line 'ext2 mount 0 0'; sleep 1; send_line 'ext2 info'; sleep 1; send_line 'ext2 stat /triple.bin'; sleep 1; send_line 'ext2 catrange /triple.bin 67383296'; sleep 1; } | socat - UNIX-CONNECT:"$SOCK"
printf 'screendump /tmp/atmkoala-ext2-triple.ppm\n' | socat - UNIX-CONNECT:"$SOCK"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-ext2-triple.ppm > /tmp/atmkoala-ext2-triple.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-ext2-triple.ppm /tmp/atmkoala-ext2-triple.png; fi
