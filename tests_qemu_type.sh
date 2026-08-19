#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
key_for() {
  case "$1" in
    ' ') printf spc;; '/') printf slash;; '-') printf minus;; '.') printf dot;;
    '_') printf shift-minus;; *) printf '%s' "$1";;
  esac
}
send_line() {
  local text="$1" c key i
  for ((i=0; i<${#text}; i++)); do
    c="${text:i:1}"; key="$(key_for "$c")"
    printf 'sendkey %s\n' "$key"
  done
  printf 'sendkey ret\n'
}
{
  send_line 'ext2 mount 0 0'; sleep 1
  send_line 'ext2 info'; sleep 1
  send_line 'ext2 ls -l /'; sleep 1
  send_line 'ext2 stat /indirect.bin'; sleep 1
  send_line 'ext2 readlink /marker-link'; sleep 1
  send_line 'ext2 catrange /indirect.bin 49152'; sleep 1
  send_line 'ext2 catrange /indirect.bin 4243456'; sleep 1
  send_line 'cpucompat'; sleep 1
} | socat - UNIX-CONNECT:"$SOCK"
