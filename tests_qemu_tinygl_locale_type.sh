#!/usr/bin/env bash
set -euo pipefail
SOCK=/tmp/atmkoala-qemu.mon
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
sleep 8
cmd 'sendkey f2'; sleep 1; cmd 'sendkey 0'; cmd 'sendkey ret'; sleep 2
cmd 'screendump /tmp/atmkoala-tinygl.ppm'
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-tinygl.ppm > /tmp/atmkoala-tinygl.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-tinygl.ppm /tmp/atmkoala-tinygl.png; fi
cmd 'sendkey alt-f4'; sleep 1; cmd 'sendkey f2'; sleep 1; cmd 'sendkey 5'; cmd 'sendkey ret'; sleep 1; cmd 'sendkey right'; sleep 1; cmd 'sendkey 2'; sleep 2
cmd 'screendump /tmp/atmkoala-locale-ru.ppm'
if command -v pnmtopng >/dev/null 2>&1; then pnmtopng /tmp/atmkoala-locale-ru.ppm > /tmp/atmkoala-locale-ru.png; else ffmpeg -y -loglevel error -i /tmp/atmkoala-locale-ru.ppm /tmp/atmkoala-locale-ru.png; fi
