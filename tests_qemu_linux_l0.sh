#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.9-limine.iso}
SOCK=/tmp/atmkoala-linux-l0.mon
CAPTURE=/tmp/atmkoala-linux-l0.ppm
PNG=/tmp/atmkoala-linux-l0.png
LOG=/tmp/atmkoala-linux-l0.log
SERIAL=/tmp/atmkoala-linux-l0.serial
rm -f "$SOCK" "$CAPTURE" "$PNG" "$LOG" "$SERIAL"
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" \
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
[ -S "$SOCK" ] || { echo 'QEMU monitor did not start' >&2; exit 1; }
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
send_text(){
  local s="$1" c k
  for ((i=0;i<${#s};i++)); do
    c="${s:i:1}"
    case "$c" in ' ') k=spc;; *) k="$c";; esac
    cmd "sendkey $k"
  done
}
sleep 9
send_text 'posix test'
cmd 'sendkey ret'
sleep 5
cmd "screendump $CAPTURE"
if command -v pnmtopng >/dev/null 2>&1; then
  pnmtopng "$CAPTURE" >"$PNG"
else
  convert "$CAPTURE" "$PNG"
fi
grep -Fq '[boot] splash-shown' "$SERIAL" || {
  echo 'Graphical boot splash did not report serial [boot] splash-shown' >&2
  exit 1
}
grep -Fq '[linux] l0-ok' "$SERIAL" || {
  echo 'Linux L0 regression did not report serial [linux] l0-ok' >&2
  exit 1
}
grep -Fq '[linux] descriptor-ok' "$SERIAL" || {
  echo 'Linux descriptor regression did not report serial [linux] descriptor-ok' >&2
  exit 1
}
grep -Fq '[linux] session-ok' "$SERIAL" || {
  echo 'Linux process-group/session regression did not report serial [linux] session-ok' >&2
  exit 1
}
grep -Fq '[linux] v22-ok' "$SERIAL" || {
  echo 'Linux descriptor metadata, positional-vector, fchdir, getrusage, times, getrlimit, faccessat, namespace-at and signal-0 regression did not report serial [linux] v22-ok' >&2
  exit 1
}
grep -Fq '[linux] l1-ok' "$SERIAL" || {
  echo 'Linux L1 regression did not report serial [linux] l1-ok' >&2
  exit 1
}
grep -Fq '[linux] l3-ok' "$SERIAL" || {
  echo 'Linux L3 regression did not report serial [linux] l3-ok' >&2
  exit 1
}
grep -Fq '[exec] ok' "$SERIAL" || {
  echo 'Native exec regression did not report serial [exec] ok' >&2
  exit 1
}
grep -Fq '[native] cpl3-signal-ok' "$SERIAL" || {
  echo 'CPL3 direct-child signal regression did not report serial [native] cpl3-signal-ok' >&2
  exit 1
}
grep -Fq '[libc] smoke-ok' "$SERIAL" || {
  echo 'POSIX static libc regression did not report serial [libc] smoke-ok' >&2
  exit 1
}
grep -Fq '[vbe] fastpath-ok' "$SERIAL" || {
  echo 'VBE fast-path regression did not report serial [vbe] fastpath-ok' >&2
  exit 1
}
grep -Fq '[vbe] geometry-ok' "$SERIAL" || {
  echo 'VBE geometry regression did not report serial [vbe] geometry-ok' >&2
  exit 1
}
grep -Fq '[vbe] exp-backbuffer-static' "$SERIAL" || {
  echo 'Adaptive Exp path did not report reserved static full-frame backbuffer' >&2
  exit 1
}
grep -Fq '[mouse] packet-ok' "$SERIAL" || {
  echo 'Mouse packet validation did not report serial [mouse] packet-ok' >&2
  exit 1
}
grep -Fq '[udp] parser-ok' "$SERIAL" || {
  echo 'Bounded UDP parser regression did not report serial [udp] parser-ok' >&2
  exit 1
}
grep -Fq '[ntp] parser-ok' "$SERIAL" || {
  echo 'Manual NTP parser regression did not report serial [ntp] parser-ok' >&2
  exit 1
}
grep -Fq '[http] parser-ok' "$SERIAL" || {
  echo 'Bounded HTTP parser regression did not report serial [http] parser-ok' >&2
  exit 1
}
grep -Fq '[mp3] parser-ok' "$SERIAL" || {
  echo 'Bounded MP3 parser regression did not report serial [mp3] parser-ok' >&2
  exit 1
}
grep -Fq '[hda] detect-ok' "$SERIAL" || {
  echo 'Read-only HDA discovery regression did not report serial [hda] detect-ok' >&2
  exit 1
}
grep -Fq '[uhd600] detect-ok' "$SERIAL" || {
  echo 'UHD 600 discovery regression did not report serial [uhd600] detect-ok' >&2
  exit 1
}
grep -Fq '[hardware] status-ok' "$SERIAL" || {
  echo 'Hardware-status regression did not report serial [hardware] status-ok' >&2
  exit 1
}
grep -Fq '[installer] ui-ok' "$SERIAL" || {
  echo 'Installer UI regression did not report serial [installer] ui-ok' >&2
  exit 1
}
grep -Fq '[init] runtime-ok' "$SERIAL" || {
  echo 'Init runtime regression did not report serial [init] runtime-ok' >&2
  exit 1
}
grep -Fq '[exp] utf8-layout-ok' "$SERIAL" || {
  echo 'Exp UTF-8 layout regression did not report serial [exp] utf8-layout-ok' >&2
  exit 1
}
grep -Fq '[time] timezone-ok' "$SERIAL" || {
  echo 'Timezone conversion regression did not report serial [time] timezone-ok' >&2
  exit 1
}
grep -Fq '[rtc] writer-ok' "$SERIAL" || {
  echo 'RTC writer encoding regression did not report serial [rtc] writer-ok' >&2
  exit 1
}
grep -Fq '[tzif] parser-ok' "$SERIAL" || {
  echo 'Bounded TZif parser regression did not report serial [tzif] parser-ok' >&2
  exit 1
}
grep -Fq '[pkg] repo-ok' "$SERIAL" || {
  echo 'Package repository URL regression did not report serial [pkg] repo-ok' >&2
  exit 1
}
grep -Fq '[ixpy] parser-raw-source-ok' "$SERIAL" || {
  echo 'Restricted ixpy parser/raw-source regression did not report serial [ixpy] parser-raw-source-ok' >&2
  exit 1
}
echo 'Graphical ATMKoala boot splash plus Linux x86-64 SYSCALL L0/L1/L3, bounded UDP, manual NTP, RTC writer encoding and TZif parser checks, descriptor flags/metadata, process-group/session calls, positional vector I/O, fchdir, getrusage, times, getrlimit, faccessat and namespace-at, native exec, CPL3 direct-child signal delivery, package repository, HTTP parser, MP3 parser, HDA discovery, UHD 600 discovery, hardware status, Disk Install UI, init runtime, Exp UTF-8 text layout, timezone conversion, VBE fast-path, mouse packet validation, restricted ixpy parser/raw-source and POSIX static-libc regressions passed.'
