#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
key_for(){ case "$1" in ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;; *) printf '%s' "$1";; esac; }
send_line(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; printf 'sendkey %s\n' "$(key_for "$c")"; done; printf 'sendkey ret\n'; }
sleep 8
{ send_line 'btrfs probe 0 0'; sleep 1; send_line 'btrfs status'; sleep 1; } | socat - UNIX-CONNECT:"$SOCK"
printf 'screendump /tmp/atmkoala-btrfs-qemu.ppm\n' | socat - UNIX-CONNECT:"$SOCK"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-btrfs-qemu.ppm > /tmp/atmkoala-btrfs-qemu.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-btrfs-qemu.ppm /tmp/atmkoala-btrfs-qemu.png; fi
