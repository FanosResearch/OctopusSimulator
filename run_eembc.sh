#!/usr/bin/env bash
#
# run_eembc.sh -- run the EEMBC trace benchmarks (4 cores each) through the
# Octopus simulator and summarize per-benchmark latency.
#
# Each benchmark under BMs/eembc-traces/<name>-trace/ ships four core traces
# (trace_C0..C3.trc.shared) and runs directly on the 4-core MultiCoreSystem
# configuration. The simulator writes per-request latency reports into a
# `newLogger/` directory *inside each workload dir* -- but it does NOT create
# that directory, so this script creates it first (otherwise the report
# ofstream fails silently and no results are produced).
#
# Usage:
#   ./run_eembc.sh [bench ...]        # default: all *-trace benchmarks
#
# Environment overrides:
#   TIMEOUT=<sec>   per-benchmark wall-clock cap (default 120; 0 = run to
#                   completion). Reports populate incrementally, so a capped
#                   run still yields tens of thousands of completed requests.
#   SYSTEM=<name>   system configuration (default MultiCoreSystem)
#   TRACES_DIR=...  benchmark root (default BMs/eembc-traces)
#   RESULTS_DIR=... where to write the summary (default eembc-results/)
#
# Examples:
#   ./run_eembc.sh                       # all benchmarks, 120s each
#   TIMEOUT=0 ./run_eembc.sh a2time01-trace   # one benchmark, to completion
#   TIMEOUT=60 ./run_eembc.sh cacheb01-trace ttsprk01-trace
#
set -u

OCTOPUS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACES_DIR="${TRACES_DIR:-$OCTOPUS_ROOT/BMs/eembc-traces}"
SYSTEM="${SYSTEM:-MultiCoreSystem}"
TIMEOUT="${TIMEOUT:-120}"
RESULTS_DIR="${RESULTS_DIR:-$OCTOPUS_ROOT/eembc-results}"

# --- locate the simulator binary (Windows .exe or Linux) ------------------
if [ -x "$OCTOPUS_ROOT/build/Octopus_Simulator.exe" ]; then
    BIN="$OCTOPUS_ROOT/build/Octopus_Simulator.exe"
    # MinGW runtime DLLs must be on PATH for the Windows build to launch.
    MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
    [ -d "$MINGW" ] && export PATH="$MINGW:$PATH"
elif [ -x "$OCTOPUS_ROOT/build/Octopus_Simulator" ]; then
    BIN="$OCTOPUS_ROOT/build/Octopus_Simulator"
else
    echo "ERROR: build/Octopus_Simulator[.exe] not found -- build the project first." >&2
    exit 1
fi

# --- benchmark selection ---------------------------------------------------
if [ "$#" -gt 0 ]; then
    BENCHES=("$@")
else
    BENCHES=()
    for d in "$TRACES_DIR"/*-trace; do
        [ -d "$d" ] && BENCHES+=("$(basename "$d")")
    done
fi
[ "${#BENCHES[@]}" -eq 0 ] && { echo "No benchmarks found under $TRACES_DIR" >&2; exit 1; }

mkdir -p "$RESULTS_DIR"
SUMMARY="$RESULTS_DIR/summary.csv"
echo "benchmark,status,wall_s,final_cycle,completed_reqs,mean_total_latency,mean_effective_latency" > "$SUMMARY"

printf "%-16s %-8s %7s %15s %11s %11s %10s\n" \
       benchmark status wall_s final_cycle reqs mean_tot mean_eff
printf '%s\n' "--------------------------------------------------------------------------------------"

for b in "${BENCHES[@]}"; do
    wp="$TRACES_DIR/$b"
    if [ ! -f "$wp/trace_C0.trc.shared" ]; then
        printf "%-16s %-8s\n" "$b" "SKIP(no-trace)"
        continue
    fi

    # Create (and clear) the logger output directory -- required, or the
    # simulator silently drops all latency reports.
    mkdir -p "$wp/newLogger"
    rm -f "$wp/newLogger"/*.csv 2>/dev/null

    # The Windows binary wants a native (C:/...) path.
    wp_arg="$(cygpath -m "$wp" 2>/dev/null || echo "$wp")/"
    logf="$RESULTS_DIR/$b.stdout"; errf="$RESULTS_DIR/$b.stderr"

    start=$(date +%s)
    if [ "$TIMEOUT" -gt 0 ]; then
        timeout "${TIMEOUT}s" "$BIN" -s "$SYSTEM" -p "workload_path(s)=$wp_arg" >"$logf" 2>"$errf"
        rc=$?
    else
        "$BIN" -s "$SYSTEM" -p "workload_path(s)=$wp_arg" >"$logf" 2>"$errf"; rc=$?
    fi
    wall=$(( $(date +%s) - start ))
    [ "$rc" -eq 124 ] && status="timeout" || status="done"

    # Final simulation cycle = largest integer printed to stdout.
    final=$(grep -oE '[0-9]+' "$logf" 2>/dev/null | sort -n | tail -1); final=${final:-0}

    # Aggregate latency across all four cores' reports.
    #   col 12 = Total Latency, col 13 = Effective Latency (see report header).
    read -r reqs mtot meff < <(awk -F, '
        FNR==1 { next }                       # skip per-file header
        NF>=13 { c++; tot+=$12; eff+=$13 }
        END { if (c>0) printf "%d %.1f %.1f\n", c, tot/c, eff/c; else print "0 0.0 0.0" }
    ' "$wp/newLogger"/LatencyReport_C*.csv 2>/dev/null)
    reqs=${reqs:-0}; mtot=${mtot:-0.0}; meff=${meff:-0.0}

    printf "%-16s %-8s %7s %15s %11s %11s %10s\n" \
           "$b" "$status" "$wall" "$final" "$reqs" "$mtot" "$meff"
    echo "$b,$status,$wall,$final,$reqs,$mtot,$meff" >> "$SUMMARY"
done

echo ""
echo "Summary CSV : $SUMMARY"
echo "Per-request : $TRACES_DIR/<bench>/newLogger/LatencyReport_C*.csv"
echo "Caps in use : num_mshr / pwb_size / processing_queue_size (see configuration/*.csv)"
