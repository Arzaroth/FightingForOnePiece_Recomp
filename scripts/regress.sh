#!/usr/bin/env bash
# usage: scripts/regress.sh <build-dir> <out-dir>
# Scripted route for the multi-port regression run (PS2Recomp tools/port_regress.sh): boot (movies skipped by the game
# override), title, main menu, character select, VS screen, into the Luffy vs Ace fight, then REGRESS_FIGHT_SECS of
# the fight. Exits 0 once the run itself completed; the verdict is in <out-dir>/summary.txt (key=value, NA when
# unknown). Also writes run.log, last_frame.png (last_frame.raw, RGBA, without a PNG), errors.txt and details.txt.
#   route_reached     1 once the game's scene word (0x52C34C) reads 0x35 (fight) and its sub-state (0x52C350) 3
#                     (after GO), from the 5 s state dump
#   exit              ok, hang (process alive, presented frame unchanged for REGRESS_HANG_SECS), crash (process
#                     ended early), timeout (still alive 10 s after being told to stop)
#   gs_frames_*, fps_mean, frames_presented
#                     game frames (the counter at 0x52C354) per 5 s state dump, over the fight once reached, else
#                     over the run; fps_p10 and frames_over_25ms are NA: the runtime logs no per-frame present times
#   REGRESS_HANG_SECS   default 20
#   REGRESS_FIGHT_SECS  length of the fight once reached (default 45)
#   REGRESS_ROUTE_SECS  give up on reaching the fight after this long (default 180)
#   FFOP_ELF            game ELF (default: game/SLPS_255.45 of this repo)
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ $# -eq 2 ] || { echo "usage: $0 <build-dir> <out-dir>" >&2; exit 2; }
target=$1
out=$2
elf=${FFOP_ELF:-$root/game/SLPS_255.45}
hang_secs=${REGRESS_HANG_SECS:-20}
fight_secs=${REGRESS_FIGHT_SECS:-45}
route_secs=${REGRESS_ROUTE_SECS:-180}
period=5

runner=
if [ -f "$target" ] && [ -x "$target" ]; then
    runner=$target
else
    for candidate in "$target/PS2Recomp/ps2xRuntime/ps2EntryRunner" "$target/build/game/PS2Recomp/ps2xRuntime/ps2EntryRunner" \
        "$target/ps2xRuntime/ps2EntryRunner" "$target/ps2EntryRunner"; do
        if [ -x "$candidate" ]; then runner=$candidate; break; fi
    done
fi
[ -n "$runner" ] || { echo "no ps2EntryRunner in $target" >&2; exit 2; }
[ -f "$elf" ] || { echo "missing $elf" >&2; exit 2; }
runner=$(realpath "$runner")
ps2recomp=$(git -C "$root/PS2Recomp" rev-parse HEAD 2>/dev/null || echo NA)

mkdir -p "$out"
out=$(realpath "$out")
frames=$out/frames
shots=$out/shots
log=$out/run.log
rm -rf "$frames" "$shots" "$out"/last_frame.* "$out/summary.txt" "$out/details.txt" "$out/errors.txt"
mkdir -p "$frames" "$shots"
: > "$log"

pid=
stop_runner() { if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi; }
trap stop_runner EXIT
trap 'stop_runner; exit 143' TERM INT

start=$(date +%s)
(
    cd "$out"
    exec env DISPLAY="${DISPLAY:-:0}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}" PS2X_DEBUG_UI=0 \
        PS2X_SPU2_STATS=1 PS2X_STATE_DUMP=$period PS2X_WATCH=0x52C34C:3 \
        PS2X_FRAME_DUMP="$frames" PS2X_FRAME_DUMP_EVERY=1 \
        PS2X_CAPTURE_DIR="$shots" PS2X_CAPTURE_INTERVAL=$period \
        PS2X_PAD_SCRIPT="22:START/800,27:CIRCLE/400,35:CIRCLE/400,43:CIRCLE/400" \
        "$runner" "$elf"
) > "$log" 2>&1 &
pid=$!

# Prints "<sample> <scene> <sub-state> <frame counter>" per state dump, numbered from 1.
watch_samples() { grep -a '^\[state\] watch 0x52c34c:' "$log" | awk '{ print NR, $4, $5, $6 }'; }
fight_sample() { watch_samples | awk '$2 == "35" && $3 == "3" { print $1; exit }'; }

result=ok
status=NA
last_hash=
same_since=$start
fight_at_s=NA
stop_at=$(( start + route_secs ))
while :; do
    if ! kill -0 "$pid" 2>/dev/null; then
        set +e; wait "$pid"; status=$?; set -e
        pid=
        result=crash
        break
    fi
    now=$(date +%s)
    if [ "$fight_at_s" = NA ] && [ -n "$(fight_sample)" ]; then
        fight_at_s=$(( now - start ))
        stop_at=$(( now + fight_secs ))
    fi
    [ "$now" -lt "$stop_at" ] || break
    newest=$(ls "$frames" | sort -t_ -k2 -n | tail -1)
    if [ -n "$newest" ]; then
        hash=$(sha1sum "$frames/$newest" | cut -d' ' -f1)
        ls "$frames" | sort -t_ -k2 -n | head -n -1 | sed "s|^|$frames/|" | xargs -r rm -f
        if [ "$hash" != "$last_hash" ]; then
            last_hash=$hash
            same_since=$now
        elif [ $(( now - same_since )) -ge "$hang_secs" ]; then
            result=hang
            break
        fi
    fi
    sleep 1
done
duration=$(( $(date +%s) - start ))
if [ -n "$pid" ]; then
    kill "$pid" 2>/dev/null || true
    for _ in $(seq 10); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
        [ "$result" != ok ] || result=timeout
    fi
    set +e; wait "$pid"; status=$?; set -e
    pid=
fi

last=$(ls "$shots" | sort | tail -1)
if [ -n "$last" ]; then
    mv "$shots/$last" "$out/last_frame.png"
else
    last=$(ls "$frames" | sort -t_ -k2 -n | tail -1)
    if [ -n "$last" ]; then mv "$frames/$last" "$out/last_frame.raw"; fi
fi
rm -rf "$frames" "$shots"

fight=$(fight_sample)
route_reached=0
[ -z "$fight" ] || route_reached=1
frame_deltas() { watch_samples | awk '{ v = strtonum("0x" $4); if (NR > 1) print $1, v - prev; prev = v }'; }
if [ -n "$fight" ]; then
    scope=fight
    samples=$(frame_deltas | awk -v f="$fight" '$1 > f { print $2 }')
else
    scope=run
    samples=$(frame_deltas | awk 'seen || $2 > 0 { seen = 1; print $2 }')
fi
read -r median minimum mean count < <(echo "$samples" | sort -n | awk -v p=$period 'NF { v[++n] = $1; s += $1 }
    END {
        if (!n) { print "NA NA NA 0"; exit }
        m = n % 2 ? v[(n + 1) / 2] : (v[n / 2] + v[n / 2 + 1]) / 2
        printf "%s %s %.2f %d\n", m, v[1], s / (n * p), n
    }')
frames_presented=$(watch_samples | awk '{ v = strtonum("0x" $4) } END { print NR ? v : "NA" }')

errors='error|fatal|abort|exception|unimplemented|unhandled|invalid|crash|assert'
underruns=$(grep -ao '^\[spu2:stats\].* underruns [0-9]*' "$log" | tail -1 | awk '{ print $NF }')
grep -a -v '^\[state\]' "$log" | grep -a -i -E "$errors" \
    | sed -E 's/0x[0-9a-fA-F]+/X/g; s/[0-9]+/N/g' | sort | uniq -c | sort -rn > "$out/errors.txt" || true
error_lines=$(grep -a -v '^\[state\]' "$log" | grep -a -c -i -E "$errors" || true)
warning_lines=$(grep -a -v '^\[state\]' "$log" | grep -a -c -i 'warn' || true)

cat > "$out/summary.txt" <<EOF
port=ffop
ps2recomp=$ps2recomp
route_reached=$route_reached
exit=$result
duration_s=$duration
frames_presented=$frames_presented
fps_mean=$mean
fps_p10=NA
frames_over_25ms=NA
gs_frames_per_5s_median=$median
gs_frames_per_5s_min=$minimum
spu2_underruns=${underruns:-NA}
error_lines=$error_lines
warning_lines=$warning_lines
EOF
cat > "$out/details.txt" <<EOF
fight_at_s=$fight_at_s
gs_frames_scope=$scope
gs_samples=$count
runner_status=$status
runner=$runner
EOF
cat "$out/summary.txt"
