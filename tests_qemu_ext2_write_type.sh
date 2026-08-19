#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
key_for(){ case "$1" in ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;; [A-Z]) printf 'shift-%s' "${1,,}";; *) printf '%s' "$1";; esac; }
send_line(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; printf 'sendkey %s\n' "$(key_for "$c")"; done; printf 'sendkey ret\n'; }
sleep 8
{ send_line 'ext2 mount 0 0'; sleep 1; send_line 'ext2 rw on'; sleep 1; send_line 'ext2 write /indirect.bin 0 DIRECT-WRITE-OK'; sleep 1; send_line 'ext2 catrange /indirect.bin 0'; sleep 1; send_line 'ext2 rw off'; sleep 1; } | socat - UNIX-CONNECT:"$SOCK"
printf 'screendump /tmp/atmkoala-ext2-write.ppm\n' | socat - UNIX-CONNECT:"$SOCK"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-ext2-write.ppm > /tmp/atmkoala-ext2-write.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-ext2-write.ppm /tmp/atmkoala-ext2-write.png; fi
