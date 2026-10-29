#!/usr/bin/env bash
# Recompiles the game's IOP modules into generated/iop/: one boot with PS2X_IOP_DUMP_MODULES dumps each module as the
# IOP loaded it, then PS2Recomp's tools/iop_recomp.py turns each into C++. The runtime runs a module natively only
# when its file hash and base match the dump, so a change of disc or load order falls back to the interpreter.
# Usage: scripts/recomp_iop.sh [dump-dir]   (an existing dump, else a 60 s boot makes one)
source "$(dirname "$0")/common.sh"
RUNNER="$GAME_BUILD/PS2Recomp/ps2xRuntime/ps2EntryRunner"
dump="${1:-}"
if [ -z "$dump" ]; then
    [ -x "$RUNNER" ] || { echo "no $RUNNER, run scripts/build_runtime.sh first"; exit 1; }
    dump="$ROOT/build/iop-dump"
    rm -rf "$dump"
    mkdir -p "$dump"
    (cd "$ROOT/build" && PS2X_DEBUG_UI=0 PS2X_IOP_DUMP_MODULES="$dump" timeout 60 "$RUNNER" "$ELF" >/dev/null 2>&1) || true
fi
ls "$dump"/*.json >/dev/null 2>&1 || { echo "no module dump in $dump"; exit 1; }
rm -rf "$ROOT/build/iop-staging"
mkdir -p "$ROOT/build/iop-staging" "$GENERATED/iop"
for module in "$dump"/*.json; do
    python3 -I "$ROOT/PS2Recomp/tools/iop_recomp.py" "$module" --out "$ROOT/build/iop-staging" | sed -n 1p
done
rsync -rc --delete "$ROOT/build/iop-staging/" "$GENERATED/iop/"
echo "Recompiled $(find "$GENERATED/iop" -name '*.cpp' | wc -l) IOP module(s) into $GENERATED/iop"
