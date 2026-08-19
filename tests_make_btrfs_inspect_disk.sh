#!/bin/sh
set -eu
exec python3 "$(dirname "$0")/tests_make_btrfs_mirror_disk.py"
