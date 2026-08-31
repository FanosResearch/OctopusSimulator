#!/usr/bin/env bash
#
# run_splash.sh -- run the SPLASH-2 trace benchmarks (4 cores each) through the
# Octopus simulator at the default cache configuration, IN PARALLEL, and
# summarize latency.
#
# The simulator is single-threaded, so each benchmark is one process; running
# several at once uses several host cores. This is safe here because every
# benchmark reads the SAME (default) config at startup -- nothing is mutated --
# and each writes only into its own <workload>/newLogger/ directory.
#
# Layout: each benchmark is a plain sub-dir of BMs/splash/ (barnes, fft, ...)
# holding trace_C0..C3.trc.shared. A dir lacking trace_C0.trc.shared (e.g.
# lu_contig) is skipped.
#
# By default this runs every benchmark TO COMPLETION (TIMEOUT=0). SPLASH traces
# are millions of refs per core (up to ~72M), so the largest (raytrace,
# radiosity) can take many hours; parallelism makes the *wall-clock* roughly the
# single slowest benchmark rather than the sum. Set TIMEOUT>0 for a capped
# sample instead. Memory stays bounded (the logger frees each completed
# request); per-request CSVs grow on disk (~80 B/request).
#
# As requested: DEFAULT cache configuration only (no cap sweep).
#
# Usage:
#   ./run_splash.sh [bench ...]        # default: all splash benchmarks
#
# Env overrides:
#   JOBS=<n>        max benchmarks to run concurrently (default: cores-2)
#   TIMEOUT=<sec>   per-benchmark wall cap (default 0 = run to completion)
#   SYSTEM=<name>   system configuration (default MultiCoreSystem)
#   TRACES_DIR=...  benchmark root (default BMs/splash)
#   RESULTS_DIR=... summary location (default splash-results/)
#
set -u

OCTOPUS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACES_DIR="${TRACES_DIR:-$OCTOPUS_ROOT/BMs/splash}"
SYSTEM="${SYSTEM:-MultiCoreSystem}"
TIMEOUT="${TIMEOUT:-0}"
RESULTS_DIR="${RESULTS_DIR:-$OCTOPUS_ROOT/splash-results}"
NPROC="$(nproc 2>/dev/null || echo 4)"
DEFJOBS=$(( NPROC > 3 ? NPROC - 2 : 1 ))

# --- locate the simulator binary (Windows .exe or Linux) ------------------
if [ -x "$OCTOPUS_ROOT/build/Octopus_Simulator.exe" ]; then
    BIN="$OCTOPUS_ROOT/build/Octopus_Simulator.exe"
    MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
    [ -d "$MINGW" ] && export PATH="$MINGW:$PATH"
elif [ -x "$OCTOPUS_ROOT/build/Octopus_Simulator" ]; then
    BIN="$OCTOPUS_ROOT/build/Octopus_Simulator"
else
    echo "ERROR: build/Octopus_Simulator[.exe] not found -- build the project first." >&2
    exit 1
fi

# --- benchmark selection --------------------------------------------------
if [ "$#" -gt 0 ]; then
    BENCHES=("$@")
else
    # Auto-discover, ordered LARGEST-first so the giants start immediately and
    # the wall-clock (makespan) stays near the single slowest benchmark.
    BENCHES=()
    while read -r _ name; do BENCHES+=("$name"); done < <(
        for d in "$TRACES_DIR"/*/; do
            [ -f "${d}trace_C0.trc.shared" ] && echo "$(wc -l < "${d}trace_C0.trc.shared") $(basename "$d")"
        done | sort -rn
    )
fi
[ "${#BENCHES[@]}" -eq 0 ] && { echo "No SPLASH benchmarks found under $TRACES_DIR" >&2; exit 1; }

JOBS="${JOBS:-$DEFJOBS}"
[ "$JOBS" -gt "${#BENCHES[@]}" ] && JOBS=${#BENCHES[@]}

mkdir -p "$RESULTS_DIR/rows"
rm -f "$RESULTS_DIR/rows"/*.csv 2>/dev/null

# --- per-benchmark worker (runs in a background subshell) -----------------
run_one() {
    local b="$1"
    local wp="$TRACES_DIR/$b"
    if [ ! -f "$wp/trace_C0.trc.shared" ]; then
        echo "$b,skipped,0,0,0,0,0" > "$RESULTS_DIR/rows/$b.csv"
        echo "[skip ] $b (no shared trace)"
        return
    fi
    mkdir -p "$wp/newLogger"; rm -f "$wp/newLogger"/*.csv 2>/dev/null
    local wp_arg logf errf start rc wall status final reqs mtot meff
    wp_arg="$(cygpath -m "$wp" 2>/dev/null || echo "$wp")/"
    logf="$RESULTS_DIR/$b.stdout"; errf="$RESULTS_DIR/$b.stderr"
    echo "[start] $b @ $(date +%H:%M:%S)"
    start=$(date +%s)
    if [ "$TIMEOUT" -gt 0 ]; then
        timeout "${TIMEOUT}s" "$BIN" -s "$SYSTEM" -p "workload_path(s)=$wp_arg" >"$logf" 2>"$errf"; rc=$?
    else
        "$BIN" -s "$SYSTEM" -p "workload_path(s)=$wp_arg" >"$logf" 2>"$errf"; rc=$?
    fi
    wall=$(( $(date +%s) - start ))
    [ "$rc" -eq 124 ] && status="timeout" || status="done"
    final=$(grep -oE '[0-9]+' "$logf" 2>/dev/null | sort -n | tail -1); final=${final:-0}
    read -r reqs mtot meff < <(awk -F, '
        FNR==1 { next }
        NF>=13 { c++; tot+=$12; eff+=$13 }
        END { if (c>0) printf "%d %.1f %.1f\n", c, tot/c, eff/c; else print "0 0.0 0.0" }
    ' "$wp/newLogger"/LatencyReport_C*.csv 2>/dev/null)
    reqs=${reqs:-0}; mtot=${mtot:-0.0}; meff=${meff:-0.0}
    echo "$b,$status,$wall,$final,$reqs,$mtot,$meff" > "$RESULTS_DIR/rows/$b.csv"
    echo "[done ] $b  status=$status wall=${wall}s reqs=$reqs mean_tot=$mtot @ $(date +%H:%M:%S)"
}

echo "SPLASH-2 @ default config (num_mshr=$(grep -oE '^num_mshr\(i\),[^,]*' "$OCTOPUS_ROOT/configuration/CacheControllers/CacheController.csv" 2>/dev/null | cut -d, -f2), pwb_size=$(grep -oE '^pwb_size\(i\),[^,]*' "$OCTOPUS_ROOT/configuration/CacheDataHandler_COTS.csv" 2>/dev/null | cut -d, -f2))"
echo "Parallel: JOBS=$JOBS / ${NPROC} cores | TIMEOUT=$([ "$TIMEOUT" -eq 0 ] && echo 'to-completion' || echo "${TIMEOUT}s") | ${#BENCHES[@]} benchmarks"
echo "Order (largest-first): ${BENCHES[*]}"
echo ""

# --- throttled parallel launch --------------------------------------------
active=0
for b in "${BENCHES[@]}"; do
    run_one "$b" &
    active=$((active + 1))
    if [ "$active" -ge "$JOBS" ]; then wait -n; active=$((active - 1)); fi
done
wait

# --- assemble the summary (in the launch order) ---------------------------
SUMMARY="$RESULTS_DIR/summary.csv"
echo "benchmark,status,wall_s,final_cycle,completed_reqs,mean_total_latency,mean_effective_latency" > "$SUMMARY"
for b in "${BENCHES[@]}"; do
    [ -f "$RESULTS_DIR/rows/$b.csv" ] && cat "$RESULTS_DIR/rows/$b.csv" >> "$SUMMARY"
done

echo ""
echo "=========================== FINAL SUMMARY ==========================="
column -t -s, "$SUMMARY" 2>/dev/null || cat "$SUMMARY"
echo ""
echo "Summary CSV : $SUMMARY   (live per-benchmark rows in $RESULTS_DIR/rows/)"
echo "Per-request : $TRACES_DIR/<bench>/newLogger/LatencyReport_C*.csv"
