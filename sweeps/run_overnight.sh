#!/usr/bin/env bash
#
# run_overnight.sh -- launch the full configuration sweep across BOTH suites,
# parallelised as much as is safe on this machine (20 cores).
#
# Strategy (see the analysis in sweeps/README.md):
#   * Octopus reads its config ONCE at startup from a fixed absolute path, so two
#     runs with DIFFERENT configs cannot share the tree -> config changes are
#     SERIAL. Within one config, benches are independent -> run them in PARALLEL
#     (up to JOBS), largest-trace-first.
#   * SPLASH giants (raytrace 44M, radiosity 72M lines) would cost ~13 h if run
#     under all 10 axis values. Instead they run ONCE under the baseline config,
#     CONCURRENTLY with the whole sweep (their processes have already parsed the
#     baseline CSVs, so later CSV mutation cannot touch them).
#
# Result: EEMBC (9 benches) + SPLASH (10 non-giant benches) across every axis,
# plus the 2 giants under baseline, in ~3 h instead of a serial ~day.
#
# Env overrides: JOBS, EEMBC_SAFETY, SPLASH_SAFETY, GIANT_SAFETY, AXES, GIANTS.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
mkdir -p "$root/results"
LOG="$root/results/overnight.log"
ts(){ date '+%m-%d %H:%M:%S'; }
say(){ echo "[$(ts)] $*" | tee -a "$LOG"; }

MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$root/build:$PATH"
taskkill //F //IM Octopus_Simulator.exe >/dev/null 2>&1

# Memory axis is EEMBC-only: MCsim (cycle-accurate DDR4) is ~5-10x slower and
# TIMEOUTs even on the smallest SPLASH bench, so SPLASH keeps the MainMemory axes.
EEMBC_AXES="${EEMBC_AXES:-arbiter replacement cache memory}"
SPLASH_AXES="${SPLASH_AXES:-arbiter replacement cache}"
GIANTS="${GIANTS:-raytrace radiosity}"
: > "$LOG"
say "=== OVERNIGHT SWEEP START  (cores=$(nproc), JOBS=${JOBS:-auto}) ==="
say "eembc axes: $EEMBC_AXES | splash axes: $SPLASH_AXES | giants(baseline): $GIANTS"

# NOTE: giants run LAST, never concurrently with the axis sweeps. A live Octopus
# process holds the shared config file open, which makes `sed -i` in-place edits
# (gen_baseline, set_arbiter, cache size) silently fail on Windows -- appends
# still work, so it corrupts some axes and not others. So: sweep first (sed
# reliable), then giants alone.

# ---- EEMBC: all axes (fast; MCsim completes here) ----
for ax in $EEMBC_AXES; do
  say "EEMBC  $ax ..."
  SUITE=eembc SAFETY="${EEMBC_SAFETY:-180}" bash "$here/sweep_$ax.sh" >>"$LOG" 2>&1 \
    && say "EEMBC  $ax done" || say "EEMBC  $ax FAILED (rc=$?)"
done

# ---- SPLASH: MainMemory axes, giants excluded (they run separately above) ----
for ax in $SPLASH_AXES; do
  say "SPLASH $ax ..."
  SUITE=splash SAFETY="${SPLASH_SAFETY:-3000}" EXCLUDE="$GIANTS" bash "$here/sweep_$ax.sh" >>"$LOG" 2>&1 \
    && say "SPLASH $ax done" || say "SPLASH $ax FAILED (rc=$?)"
done

# ---- SPLASH giants LAST, alone (baseline config, run once) ----
if [ -n "$GIANTS" ]; then
  say "SPLASH giants (alone): $GIANTS"
  (
    export SUITE=splash SAFETY="${GIANT_SAFETY:-14400}"
    source "$here/sweep_common.sh"
    gen_baseline
    gdir="$root/results/giants"; mkdir -p "$gdir"; rm -f "$gdir"/.*.line 2>/dev/null
    for g in $GIANTS; do
      ( echo "baseline,$g,$(run_bench "$g")" > "$gdir/.${g}.line" ) &   # giants || each other only
    done
    wait
    { echo "value,benchmark,status,$METRIC_HEADER"; cat "$gdir"/.*.line 2>/dev/null; } > "$gdir/splash.csv"
  )
  say "giants done"
fi
say "=== ALL DONE ==="

# ---- status summary ----
say "---- status counts (status=count per result file) ----"
for f in "$root"/results/*/eembc.csv "$root"/results/*/splash.csv "$root"/results/giants/splash.csv; do
  [ -f "$f" ] || continue
  line="$(awk -F, 'NR>1{c[$3]++} END{for(k in c) printf "%s=%d ",k,c[k]}' "$f")"
  say "  ${f#$root/}: ${line:-<empty>}"
done
say "log: ${LOG#$root/}"
