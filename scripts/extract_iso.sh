#!/usr/bin/env bash
# Extracts the files of your own Fighting for One Piece (NTSC-J, SLPS-25545) disc image into game/.
# Usage: scripts/extract_iso.sh <image.iso>
source "$(dirname "$0")/common.sh"
[ $# -eq 1 ] && [ -f "$1" ] || { echo "usage: $0 <image.iso>"; exit 1; }
command -v bsdtar >/dev/null || { echo "bsdtar not found (libarchive)"; exit 1; }
mkdir -p "$GAME_DIR"
bsdtar -xf "$1" -C "$GAME_DIR"
chmod -R u+w "$GAME_DIR"
[ -f "$ELF" ] || { echo "no SLPS_255.45 in $1: not the NTSC-J disc?"; exit 1; }
expected=e445f2af578e3780bcd7f59cf57c57b491f3c747
actual=$(sha1sum "$ELF" | cut -d' ' -f1)
[ "$actual" = "$expected" ] || echo "warning: SLPS_255.45 SHA-1 is $actual, the port is built against $expected"
echo "Disc files in $GAME_DIR"
