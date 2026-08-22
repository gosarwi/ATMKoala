#!/usr/bin/env bash
set -euo pipefail

ISO=${1:-atmkoala-OS-v0.9-limine.iso}
DISK=/tmp/atmkoala-cfdisk-commit.img
SOCK=/tmp/atmkoala-cfdisk-commit.mon
SERIAL=/tmp/atmkoala-cfdisk-commit.serial
LOG=/tmp/atmkoala-cfdisk-commit.log
rm -f "$DISK" "$SOCK" "$SERIAL" "$LOG"

# Fresh test media only. This script must never accept a caller-selected disk.
dd if=/dev/zero of="$DISK" bs=1M count=64 status=none
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" \
  -drive file="$DISK",format=raw,if=ide \
  -monitor "unix:$SOCK,server,nowait" -serial "file:$SERIAL" -display none >"$LOG" 2>&1 &
PID=$!
cleanup(){
  if kill -0 "$PID" 2>/dev/null; then
    printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true
    wait "$PID" || true
  fi
}
trap cleanup EXIT
for _ in {1..100}; do [ -S "$SOCK" ] && break; sleep 0.1; done
[ -S "$SOCK" ] || { echo 'cfdisk QEMU monitor did not start' >&2; exit 1; }
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){
  local s=$1 c k
  for ((i=0;i<${#s};i++)); do
    c=${s:i:1}
    case "$c" in ' ') k=spc;; *) k=$c;; esac
    cmd "sendkey $k"
  done
}
press_enter(){ cmd 'sendkey ret'; }
wait_marker(){
  local marker=$1
  for _ in {1..150}; do grep -Fq "$marker" "$SERIAL" 2>/dev/null && return 0; sleep 0.1; done
  echo "cfdisk did not report expected marker: $marker" >&2
  return 1
}

# Limine menu: adaptive, 1024, 640, then Text mode.
sleep 1
for _ in 1 2 3; do cmd 'sendkey down'; done
press_enter
for _ in {1..180}; do grep -Fq 'root@atmkoala' "$SERIAL" 2>/dev/null && break; sleep 0.1; done
grep -Fq 'root@atmkoala' "$SERIAL" || { echo 'Text-mode shell prompt did not appear' >&2; exit 1; }

# Select the only fresh ATA drive; stage a 32 MiB CatFS primary partition at LBA 2048.
send_text 'cfdisk'; press_enter
press_enter; wait_marker '[cfdisk] ready'
cmd 'sendkey n'
send_text '2048'; press_enter
send_text '32'; press_enter
cmd 'sendkey n'
wait_marker '[cfdisk] staged-add'
cmd 'sendkey spc'
cmd 'sendkey w'
send_text 'write'; press_enter
wait_marker '[cfdisk] write-accepted'
wait_marker '[cfdisk] write-ok'

# Independently inspect the disposable raw image. Entry 0 must be CatFS at
# LBA 2048 with 65536 sectors; neither formatting nor any payload write occurs.
[ "$(od -An -tx1 -j 510 -N 2 "$DISK" | tr -d '[:space:]')" = '55aa' ] || { echo 'cfdisk did not write an MBR signature' >&2; exit 1; }
ENTRY=$(od -An -tx1 -j 446 -N 16 "$DISK" | tr -d '[:space:]')
[ "${ENTRY:8:2}" = 'c5' ] || { echo "cfdisk wrote unexpected partition type: $ENTRY" >&2; exit 1; }
[ "${ENTRY:16:8}" = '00080000' ] || { echo "cfdisk wrote unexpected start LBA: $ENTRY" >&2; exit 1; }
[ "${ENTRY:24:8}" = '00000100' ] || { echo "cfdisk wrote unexpected sector count: $ENTRY" >&2; exit 1; }
printf 'cfdisk staged WRITE regression passed on fresh disposable image: %s\n' "$DISK"
