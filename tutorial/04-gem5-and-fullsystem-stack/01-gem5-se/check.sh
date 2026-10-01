#!/usr/bin/env bash
# 04/01 check: gem5 SE drives Octopus correctly, and the SLAM demo tracks when it
# runs alone. Runs se_test (FCFS, then round-robin bus) and the Solo demo, all in
# parallel; about 5 minutes. Needs gem5 built with this repository as an EXTRAS
# module (GEM5_ROOT or GEM5_BIN) and the two aarch64 binaries built (README, step 1).
#
# gem5 timing depends on the gem5 build, so this checks behaviour, not cycle
# counts: se_test's self-checks pass, and the demo drops no scan, maps every
# keyframe and stays within 0.15 m of the ground truth. Reference numbers for
# the full matrix are in expected/summary.txt.

set -uo pipefail
GEM5_ROOT=${GEM5_ROOT:-}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEM5_BIN=${GEM5_BIN:-$GEM5_ROOT/build/ARM/gem5.opt}
CONFIG="$ROOT/gem5/configs/se_arm.py"
SE_TEST="$ROOT/gem5/se_test/se_test-static"
DEMO="$HERE/slam_demo/slam_demo"
OUT="$HERE/runs/check"

[ -x "$GEM5_BIN" ] || { echo "[FAIL] gem5 not found: set GEM5_ROOT or GEM5_BIN"; exit 1; }
[ -x "$SE_TEST" ] || { echo "[FAIL] se_test not built: make -C $ROOT/gem5/se_test"; exit 1; }
[ -x "$DEMO" ] || { echo "[FAIL] slam_demo not built: make -C $HERE/slam_demo"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"

"$GEM5_BIN" -re -d "$OUT/se_fcfs" "$CONFIG" --binary "$SE_TEST" > /dev/null 2>&1 &
"$GEM5_BIN" -re -d "$OUT/se_rr" "$CONFIG" --binary "$SE_TEST" \
    --octopus-param 'bus[0].interconnect_controller.arbiter_type(s)=RRArbiter' > /dev/null 2>&1 &
"$GEM5_BIN" -re -d "$OUT/solo" "$CONFIG" --num-cores 4 --binary "$DEMO" -- --period-us 80 > /dev/null 2>&1 &
wait

fail=0
for r in se_fcfs se_rr; do
    if grep -q "RESULT: PASS" "$OUT/$r/simout.txt" 2>/dev/null; then
        echo "[ok]   se_test ($r)"
    else
        echo "[FAIL] se_test ($r): see $OUT/$r/simout.txt and simerr.txt"; fail=1
    fi
done

rt=$(grep -h '^\[RT\]' "$OUT/solo/simout.txt" 2>/dev/null)
rmse=$(grep -h '^\[ATE\]' "$OUT/solo/simout.txt" 2>/dev/null | grep -oE 'rmse [0-9.]+' | cut -d' ' -f2)
dropped=$(echo "$rt" | grep -oE 'dropped [0-9]+' | cut -d' ' -f2)
mapped=$(echo "$rt" | grep -oE 'keyframes mapped [0-9]+/[0-9]+' | cut -d' ' -f3)
if [ -n "$rmse" ] && [ "$dropped" = "0" ] && [ "${mapped%/*}" = "${mapped#*/}" ] && \
   awk -v e="$rmse" 'BEGIN { exit !(e < 0.15) }'; then
    echo "[ok]   slam_demo alone: 0 dropped, keyframes $mapped, RMSE $rmse m"
else
    echo "[FAIL] slam_demo alone: dropped ${dropped:-?}, keyframes ${mapped:-?}, RMSE ${rmse:-?} m (see $OUT/solo)"; fail=1
fi

[ $fail -eq 0 ] && echo "[PASS] 04/01: gem5 SE + Octopus work; run run_matrix.sh for the interference demo"
exit $fail
