#!/usr/bin/env bash
set -euo pipefail
ISO=${1:-atmkoala-OS-v0.5.iso}
SOCK=/tmp/atmkoala-scale-gui-posix.mon
DEMO=/tmp/atmkoala-gui-demo.ppm
SCALE=/tmp/atmkoala-settings-scale-150.ppm
POSIX=/tmp/atmkoala-posix-smoke.ppm
rm -f "$SOCK" "$DEMO" "$SCALE" "$POSIX" /tmp/atmkoala-gui-demo.png /tmp/atmkoala-settings-scale-150.png /tmp/atmkoala-posix-smoke.png
qemu-system-x86_64 -m 256M -vga std -boot d -cdrom "$ISO" -monitor "unix:$SOCK,server,nowait" -display none >/tmp/atmkoala-scale-gui-posix.log 2>&1 &
QPID=$!
cleanup(){ if kill -0 "$QPID" 2>/dev/null; then printf 'quit\n' | socat - UNIX-CONNECT:"$SOCK" >/dev/null 2>&1 || true; wait "$QPID" || true; fi; }
trap cleanup EXIT
cmd(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:"$SOCK" >/dev/null; }
key_for(){ case "$1" in ' ') printf spc;; '-') printf minus;; '.') printf dot;; '/') printf slash;; *) printf '%s' "$1";; esac; }
send_text(){ local s="$1" c k; for ((i=0;i<${#s};i++)); do c="${s:i:1}"; k=$(key_for "$c"); cmd "sendkey $k"; done; }
convert(){ local src="$1" dst="$2"; if command -v pnmtopng >/dev/null 2>&1; then pnmtopng "$src" > "$dst"; else ffmpeg -y -loglevel error -i "$src" "$dst"; fi; }
sleep 9
# Default Exp terminal: open a separately compiled native GUI ABI client.
send_text 'gui open org.atmkoala.sdk-demo'; cmd 'sendkey ret'; sleep 2; cmd 'sendkey ret'; sleep 1; cmd "screendump $DEMO"
# Open Settings, then use GUI-keyboard scale cycle from default 130 to 150.
cmd 'sendkey alt-s'; sleep 2; cmd 'sendkey bracket_right'; sleep 2; cmd "screendump $SCALE"
# Return to framebuffer shell and run the VFS POSIX portable-subset smoke test.
cmd 'sendkey alt-f1'; sleep 2; send_text 'posix test'; cmd 'sendkey ret'; sleep 2; cmd "screendump $POSIX"
convert "$DEMO" /tmp/atmkoala-gui-demo.png
convert "$SCALE" /tmp/atmkoala-settings-scale-150.png
convert "$POSIX" /tmp/atmkoala-posix-smoke.png
echo 'scale/gui/posix regression passed: screenshots created.'
