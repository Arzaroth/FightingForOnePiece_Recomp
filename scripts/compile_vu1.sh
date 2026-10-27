#!/usr/bin/env bash
# Compiles the VU1 microcode the ELF uploads into generated/vu1/: the MPG uploads in its data, then the raw
# microcode blobs the game builds its uploads from at run time (scripts/vu1_blobs_elf.py).
source "$(dirname "$0")/common.sh"
[ -f "$ELF" ] || { echo "missing $ELF, run scripts/extract_iso.sh first"; exit 1; }
COMPILER="$TOOLS_BUILD/ps2xRecomp/ps2_vu_aot"
[ -x "$COMPILER" ] || "$ROOT/scripts/build_tools.sh"
rm -rf "$ROOT/build/vu1-staging"
mkdir -p "$ROOT/build/vu1-staging" "$GENERATED/vu1"
"$COMPILER" "$ELF" "$ROOT/build/vu1-staging"
python3 -I "$ROOT/scripts/vu1_blobs_elf.py" "$ELF" "$ROOT/build/vu1_blobs.elf"
"$COMPILER" "$ROOT/build/vu1_blobs.elf" "$ROOT/build/vu1-staging" vu1_blob_
rsync -rc --delete "$ROOT/build/vu1-staging/" "$GENERATED/vu1/"
echo "Compiled $(find "$GENERATED/vu1" -name '*.cpp' | wc -l) VU1 file(s) into $GENERATED/vu1"
