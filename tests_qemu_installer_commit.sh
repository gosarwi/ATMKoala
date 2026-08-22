#!/usr/bin/env bash
set -euo pipefail

ISO=${1:-atmkoala-OS-v0.9-limine.iso}
DISK=/tmp/atmkoala-installer-commit.img
SOCK=/tmp/atmkoala-installer-commit.mon
SERIAL=/tmp/atmkoala-installer-commit.serial
LOG=/tmp/atmkoala-installer-commit.log
rm -f "$SOCK" "$SERIAL" "$LOG" "$DISK"

# This image is created fresh for every run. The test intentionally exercises
# the installer MBR/CatFS transaction and must never be pointed at real media.
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
[ -S "$SOCK" ] || { echo 'Installer QEMU monitor did not start' >&2; exit 1; }
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
  for _ in {1..100}; do grep -Fq "$marker" "$SERIAL" 2>/dev/null && return 0; sleep 0.1; done
  echo "Installer did not report expected marker: $marker" >&2
  return 1
}

# Limine menu: adaptive graphics, two manual fallbacks, text, installer.
sleep 1
for _ in 1 2 3 4; do cmd 'sendkey down'; done
cmd 'sendkey ret'
for _ in {1..150}; do grep -Fq '[installer] ready' "$SERIAL" 2>/dev/null && break; sleep 0.2; done
grep -Fq '[installer] ready' "$SERIAL" || { echo 'Installer did not report ready' >&2; exit 1; }

# Step 0 Welcome; 1 target; 2 layout; 3 account setup.
press_enter; wait_marker '[installer] step-target'
press_enter; wait_marker '[installer] step-layout'
press_enter; wait_marker '[installer] step-setup'
# setup_field begins at timezone: Tab moves to root password.
cmd 'sendkey tab'; sleep 0.2
send_text 'rootpass'; sleep 0.4
press_enter; wait_marker '[installer] step-confirm'
# Step 4 destructive confirmation. This must be activated by Enter, not mouse.
send_text 'erase'; wait_marker '[installer] erase-unlocked'
press_enter
wait_marker '[installer] destructive-activation'
wait_marker '[installer] transaction-ok'

grep -Fq '[installer] destructive-activation' "$SERIAL" || {
  echo 'Installer final confirmation did not accept Enter after ERASE' >&2
  exit 1
}
grep -Fq '[installer] transaction-ok' "$SERIAL" || {
  echo 'Installer destructive transaction did not complete successfully' >&2
  exit 1
}
# Confirm that the freshly-created disposable image received an MBR signature.
[ "$(od -An -tx1 -j 510 -N 2 "$DISK" | tr -d '[:space:]')" = '55aa' ] || {
  echo 'Installer transaction did not write the expected disposable MBR signature' >&2
  exit 1
}
printf 'installer final Enter activation regression passed on fresh disposable image: %s\n' "$DISK"
