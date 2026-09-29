#!/usr/bin/env bash
# 00 -- re-run a2time01 on the shipped configuration and compare Summary.csv with the
# reference. The simulator is deterministic, so this is an equality test.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$ROOT"

BIN="build/Octopus_Simulator"; [ -x "$BIN" ] || BIN="build/Octopus_Simulator.exe"
[ -x "$BIN" ] || { echo "[FAIL] simulator not built: cmake -S . -B build && cmake --build build"; exit 1; }
W="$ROOT/BMs/eembc-traces/a2time01-trace"
[ -f "$W/trace_C0.trc.shared" ] || { echo "[FAIL] benchmark missing: ./get_benchmarks.sh"; exit 1; }

rm -f "$W/newLogger"/*.csv; mkdir -p "$W/newLogger"
wpath=$(cygpath -m "$W" 2>/dev/null || printf '%s' "$W")
if ! "$BIN" -s MultiCoreSystem -p "workload_path(s)=$wpath/" > "$W/.out.check" 2>&1; then
  echo "[FAIL] simulator exited non-zero (see $W/.out.check)"; exit 1
fi

got=$(tr -d '\r' < "$W/newLogger/Summary.csv" | md5sum | cut -d' ' -f1)
ref=$(tr -d '\r' < "$HERE/expected/Summary.csv" | md5sum | cut -d' ' -f1)
if [ "$got" = "$ref" ]; then
  echo "[PASS] 00 first run: Summary.csv matches the reference"
  exit 0
fi
echo "[FAIL] 00 first run: Summary.csv differs from the reference"
echo "       yours:"; column -s, -t "$W/newLogger/Summary.csv" | cut -c1-120 | sed 's/^/         /'
if ! git -C "$ROOT" diff --quiet -- configuration 2>/dev/null; then
  echo "       configuration/ differs from what was shipped -- 'git checkout -- configuration/' resets it"
fi
exit 1
