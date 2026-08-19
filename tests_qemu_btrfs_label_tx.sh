#!/usr/bin/env bash
set -euo pipefail

ISO=${1:-atmkoala-OS-v0.5.iso}
DISK=${2:-/tmp/atmkoala-real-btrfs.img}
SOCK=/tmp/atmkoala-btrfs-label.mon
PPM=/tmp/atmkoala-btrfs-label-tx.ppm
PNG=/tmp/atmkoala-btrfs-label-tx.png
rm -f "$SOCK" "$PPM" "$PNG"

qemu-system-x86_64 \
  -m 256M -vga std -boot d -cdrom "$ISO" -hda "$DISK" \
  -monitor "unix:$SOCK,server,nowait" -display none \
  >/tmp/atmkoala-btrfs-label-qemu.log 2>&1 &
QEMU_PID=$!
cleanup() {
  if kill -0 "$QEMU_PID" 2>/dev/null; then
    printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true
    wait "$QEMU_PID" || true
  fi
}
trap cleanup EXIT

cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK"; }
key_for(){ case "$1" in ' ')printf spc;; '-')printf minus;; *)printf '%s' "$1";; esac; }
send_line(){ local text="$1" i c; for ((i=0;i<${#text};i++)); do c="${text:i:1}";cmd "sendkey $(key_for "$c")";done;cmd 'sendkey ret'; }

sleep 8
cmd 'sendkey alt-f1'; sleep 2
send_line 'btrfs probe 0 0'; sleep 1
send_line 'btrfs rw on'; sleep 1
send_line 'btrfs label atmkoala-label-tx'; sleep 1
send_line 'btrfs status'; sleep 1
cmd "screendump $PPM"
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$PPM" > "$PNG"; else ffmpeg -y -loglevel error -i "$PPM" "$PNG"; fi

# The guest is stopped before inspecting the same disposable image from host.
cleanup
trap - EXIT
dd if="$DISK" of=/tmp/atmkoala-real-btrfs.part bs=512 skip=2048 status=none
btrfs inspect-internal dump-super /tmp/atmkoala-real-btrfs.part | grep -E '^label|^generation' | head -n 2
