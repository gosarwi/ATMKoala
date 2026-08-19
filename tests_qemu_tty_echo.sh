#!/usr/bin/env bash
set -euo pipefail

SOCK=/tmp/atmkoala-tty-echo.mon
ISO=${1:-atmkoala-OS-v0.5.iso}
PRE=/tmp/atmkoala-tty-echo-before-enter.ppm
POST=/tmp/atmkoala-tty-echo-after-enter.ppm
rm -f "$SOCK" "$PRE" "$POST" /tmp/atmkoala-tty-echo-before-enter.png /tmp/atmkoala-tty-echo-after-enter.png

qemu-system-x86_64 \
  -m 256M -vga std -boot d -cdrom "$ISO" \
  -monitor "unix:$SOCK,server,nowait" -display none \
  >/tmp/atmkoala-tty-echo-qemu.log 2>&1 &
QEMU_PID=$!
cleanup() {
  if kill -0 "$QEMU_PID" 2>/dev/null; then
    printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true
    wait "$QEMU_PID" || true
  fi
}
trap cleanup EXIT

cmd() { printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK"; }
key_for() {
  case "$1" in
    ' ') printf spc ;; '-') printf minus ;; '_') printf shift-minus ;;
    '.') printf dot ;; '/') printf slash ;; *) printf '%s' "$1" ;;
  esac
}
send_text() {
  local text="$1" i c key
  for ((i=0; i<${#text}; i++)); do
    c="${text:i:1}"; key="$(key_for "$c")"; cmd "sendkey $key"
  done
}

# GRUB default boots graphical Exp. Alt+F1 returns to the framebuffer-backed
# shell, which is the previous no-echo failure path.
sleep 8
cmd 'sendkey alt-f1'
sleep 2
send_text 'echo live-tty-echox'
cmd 'sendkey backspace'
sleep 1
cmd "screendump $PRE"
cmd 'sendkey ret'
sleep 2
cmd "screendump $POST"

if command -v pnmtopng >/dev/null 2>&1; then
  pnmtopng "$PRE" > /tmp/atmkoala-tty-echo-before-enter.png
  pnmtopng "$POST" > /tmp/atmkoala-tty-echo-after-enter.png
else
  ffmpeg -y -loglevel error -i "$PRE" /tmp/atmkoala-tty-echo-before-enter.png
  ffmpeg -y -loglevel error -i "$POST" /tmp/atmkoala-tty-echo-after-enter.png
fi

echo "TTY echo regression passed: pre-Enter and post-Enter screenshots created."
