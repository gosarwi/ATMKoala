#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
key_for(){ case "$1" in ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;; *) printf '%s' "$1";; esac; }
send_line(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; printf 'sendkey %s\n' "$(key_for "$c")"; done; printf 'sendkey ret\n'; }
sleep 8
{ send_line 'pkg create demo /proc/version'; sleep 1; send_line 'pkg info demo.atpk'; sleep 1; send_line 'pkg install demo.atpk'; sleep 1; send_line 'pkg list'; sleep 1; send_line 'cat /syls/bin/demo'; sleep 1; } | socat - UNIX-CONNECT:"$SOCK"
printf 'screendump /tmp/atmkoala-atpk-qemu.ppm\n' | socat - UNIX-CONNECT:"$SOCK"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-atpk-qemu.ppm > /tmp/atmkoala-atpk-qemu.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-atpk-qemu.ppm /tmp/atmkoala-atpk-qemu.png; fi
