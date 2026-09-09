#!/usr/bin/env bash
#
# sweep_common.sh -- shared harness for Octopus single-axis configuration sweeps.
#
# Each sweep holds a fixed BASELINE and varies ONE configuration axis, runs a
# benchmark suite, and records completion + latency metrics parsed from the
# per-run Summary.csv the Logger emits (see docs/Logger.md).
#
# Baseline: snoop MESI, TripleBus, L1-private + shared LLC, LRU replacement,
#           FCFS bus arbiter, MainMemory (fixed-latency), cache latency default.
#
# Metrics per (config,benchmark): status + avg latency, worst-case Total,
# worst-case Request-Bus, worst-case Response-Bus, worst-case DRAM, finish cycle.
# The per-axis component (bus for arbiter, DRAM for memory, ...) shows WHERE the
# knob acts.
#
# Env knobs:  SUITE=eembc|splash (default eembc)   SAFETY=<sec> (default 300)
#             BENCH=<name>  run only one benchmark (fast smoke)
#
# NOTE: preset CSVs have NO trailing newline, so appended overrides are
# newline-guarded (see set_csv) -- otherwise the line glues onto a comment and
# is silently ignored.

set -u
SWEEP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$SWEEP_ROOT/build/Octopus_Simulator.exe"
[ -f "$BIN" ] || BIN="$SWEEP_ROOT/build/Octopus_Simulator"
CFG="$SWEEP_ROOT/configuration/SystemConfigurations/MultiCoreSystem.csv"
SNOOP="$SWEEP_ROOT/configuration/SystemConfigurations/MultiCoreSystem_Snoop.csv"
SBC="$SWEEP_ROOT/configuration/Interconnect/SplitBusController.csv"

# make the runtime DLLs discoverable (MinGW UCRT + build dir)
MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
[ -d "$MINGW" ] && export PATH="$MINGW:$SWEEP_ROOT/build:$PATH"

SUITE="${SUITE:-eembc}"
SAFETY="${SAFETY:-300}"
# Parallel fan-out: within one (fixed) config, benches are independent processes
# writing to distinct newLogger dirs, so we run up to JOBS at once. Config is read
# once at process startup, so mutating the CSV between config batches is safe.
JOBS="${JOBS:-$(( $(nproc) - 2 ))}"; [ "$JOBS" -ge 1 ] 2>/dev/null || JOBS=1
# EXCLUDE: space-separated bench names to skip (e.g. SPLASH giants run separately).
EXCLUDE="${EXCLUDE:-}"
case "$SUITE" in
  eembc)  TR="$SWEEP_ROOT/BMs/eembc-traces";;
  splash) TR="$SWEEP_ROOT/BMs/splash";;
  *) echo "ERROR: SUITE must be eembc or splash" >&2; exit 1;;
esac

# Free any simulator that might hold a config file open. On Windows a running
# Octopus_Simulator keeps MultiCoreSystem.csv / SplitBusController.csv open, and
# `sed -i` (temp-file + rename) then fails SILENTLY -- leaving the un-edited
# preset, so the run uses the WRONG protocol/controller/arbiter. Portable:
# taskkill on Windows, pkill on POSIX.
_free_configs(){ taskkill //F //IM Octopus_Simulator.exe >/dev/null 2>&1 || pkill -f Octopus_Simulator >/dev/null 2>&1 || true; }

# --- baseline config (snoop MESI + exclusive controller) written to the active
# MultiCoreSystem.csv. Retried + VERIFIED: if the edit cannot land (config held
# open by a stray sim), abort loudly rather than silently run plain MSI. ---
gen_baseline(){
  local try
  for try in 1 2 3; do
    _free_configs
    cp "$SNOOP" "$CFG" 2>/dev/null
    sed -i -E \
      -e "s#^(cache_controller\[\*\]\.protocol_type\(s\),)[^,]*#\1SNOOP_MESI#" \
      -e "s#^(cache_controller\[\*\]\.fsm_filename\(s\),)[^,]*#\1MESI_splitBus_snooping#" \
      -e "s#^(llc_controller\.protocol_type\(s\),)[^,]*#\1SNOOP_LLC_MESI#" \
      -e "s#^(llc_controller\.fsm_filename\(s\),)[^,]*#\1MESI_LLC#" \
      -e "s#^(cache_controller_type\(s\),)[^,]*#\1CacheControllerExclusive#" "$CFG" 2>/dev/null
    grep -qF 'SNOOP_MESI' "$CFG" && grep -qF 'CacheControllerExclusive' "$CFG" && return 0
    sleep 1
  done
  echo "FATAL: gen_baseline could not write MESI + CacheControllerExclusive to $CFG" >&2
  echo "       (a simulator is holding the config open -- Windows sed-lock). Aborting." >&2
  exit 1
}

# set/override a system-CSV key. $1 = anchored key regex (up to the comma),
# $2 = the full replacement line. sed if present, else newline-guarded append.
# Retried + verified (same silent-lock hazard as gen_baseline).
set_csv(){
  local keyre="$1" line="$2" try
  for try in 1 2 3; do
    _free_configs
    if grep -qE "^${keyre}" "$CFG"; then
      sed -i -E "s#^(${keyre}).*#${line}#" "$CFG" 2>/dev/null
    else
      printf '\n%s\n' "$line" >> "$CFG"
    fi
    grep -qF "$line" "$CFG" && return 0
    sleep 1
  done
  echo "FATAL: set_csv could not apply '$line' to $CFG (config locked). Aborting." >&2
  exit 1
}

# set the interconnect (bus) arbiter -- lives in the controller's Extends file,
# NOT the system CSV. Retried + verified.
set_arbiter(){
  local try
  for try in 1 2 3; do
    _free_configs
    sed -i -E "s#^(arbiter_type\(s\),)[^,]*#\1$1#" "$SBC" 2>/dev/null
    grep -qF "arbiter_type(s),$1" "$SBC" && return 0
    sleep 1
  done
  echo "FATAL: set_arbiter could not set '$1' in $SBC (config locked). Aborting." >&2
  exit 1
}

# max-across-cores worst-case for EVERY pipeline stage + mean average, from a
# Summary.csv. cols (1-indexed): 1 CoreId, 2 WC-L1Stall, 3 WC-ReqBus,
# 4 WC-L2Stall, 5 WC-L2Access, 6 WC-RespBus, 7 WC-L2DRAMBus, 8 WC-DRAM,
# 9 WC-Total, 10 WC-Effective, 11 Average, 12 Finish. Capturing all stages lets
# the plots show a full per-stage latency breakdown (where each knob acts).
metrics(){
  awk -F, 'NR>1&&NF>=12{
             if($2>l1)l1=$2; if($3>rq)rq=$3; if($4>l2s)l2s=$4; if($5>l2a)l2a=$5;
             if($6>rp)rp=$6; if($7>db)db=$7; if($8>dr)dr=$8; if($9>wt)wt=$9;
             if($10>ef)ef=$10; a+=$11; n++; if($12>f)f=$12
           }
           END{ if(n) printf "%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", \
                          a/n, wt, ef, l1, rq, l2s, l2a, rp, db, dr, f;
                else  printf "NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA" }' "$1" 2>/dev/null
}
METRIC_HEADER="avg,wcTotal,wcEff,wcL1stall,wcReqBus,wcL2stall,wcL2access,wcRespBus,wcDramBus,wcDRAM,finish"

# run one benchmark under the active CFG. echoes: status,<6 metrics>
run_bench(){
  local b="$1"; local wp="$TR/$b"   # split: under set -u a single `local` expands
                                     # $b before it is assigned (fails unless the
                                     # caller's scope already defines b)
  [ -f "$wp/trace_C0.trc.shared" ] && { echo -n; } || { echo "SKIP,NA,NA,NA,NA,NA,NA"; return; }
  mkdir -p "$wp/newLogger"; rm -f "$wp/newLogger"/*.csv 2>/dev/null
  timeout "${SAFETY}s" "$BIN" -s MultiCoreSystem \
      -p "workload_path(s)=$(cygpath -m "$wp")/" >/dev/null 2>"$wp/.sweep.stderr"
  local rc=$? flt d=0 nc=0 c refs rows fin
  flt=$(grep -aoiE 'fault|invalid|segmentation|abort|unfound|not found|bad_function|full buffer|wrong destination' "$wp/.sweep.stderr" 2>/dev/null | head -1)
  for c in 0 1 2 3; do
    [ -f "$wp/trace_C$c.trc.shared" ] || continue; nc=$((nc+1))
    refs=$(wc -l < "$wp/trace_C$c.trc.shared")
    rows=$(awk 'END{print NR}' "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); rows=${rows:-0}
    fin=$(grep -c "Average Latency" "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); fin=${fin:-0}
    { [ "$fin" -ge 1 ] && [ "$rows" -ge "$refs" ]; } && d=$((d+1))
  done
  local st
  if   [ -n "$flt" ];                              then st="FAULT"
  elif [ "$rc" -eq 124 ];                          then st="TIMEOUT"
  elif [ "$d" -eq "$nc" ] && [ "$nc" -gt 0 ];      then st="OK"
  else st="INCOMPLETE"; fi
  echo "$st,$(metrics "$wp/newLogger/Summary.csv")"
}

# benchmark list, LARGEST-TRACE-FIRST (better packing: the long pole starts
# immediately, short benches fill in behind it). BENCH= forces a single bench;
# EXCLUDE= drops named benches (e.g. giants swept separately).
benches(){
  if [ -n "${BENCH:-}" ]; then echo "$BENCH"; return; fi
  local d b f
  for d in "$TR"/*/; do
    f="${d}trace_C0.trc.shared"; [ -f "$f" ] || continue
    b="$(basename "$d")"
    case " $EXCLUDE " in *" $b "*) continue;; esac
    printf '%d %s\n' "$(wc -l < "$f")" "$b"
  done | sort -rn | awk '{print $2}'
}

# run one axis: $1=axis name, $2=column header for the value, then a function
# `apply_value <value>` must be defined by the caller and `VALUES` set.
# Within each config value, benches run in PARALLEL (up to JOBS); a barrier
# between config values keeps the on-disk CSV constant while jobs read it at
# startup. Emits results/<axis>/<suite>.csv and prints a table.
run_axis(){
  local axis="$1" vcol="$2"
  local out="$SWEEP_ROOT/results/$axis"; mkdir -p "$out"
  local csv="$out/${SUITE}.csv"   # suite-aware: eembc.csv vs splash.csv (no clobber)
  local parts="$out/.parts_${SUITE}"; rm -rf "$parts"; mkdir -p "$parts"
  echo "$vcol,benchmark,status,$METRIC_HEADER" > "$csv"
  local v b
  for v in $VALUES; do
    apply_value "$v"
    for b in $(benches); do
      while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
      { echo "$v,$b,$(run_bench "$b")" > "$parts/${v}..${b}.line"; } &
    done
    wait                       # barrier: finish this config before it changes
  done
  # assemble deterministically (value order x largest-first bench order)
  for v in $VALUES; do
    for b in $(benches); do
      [ -f "$parts/${v}..${b}.line" ] && cat "$parts/${v}..${b}.line" >> "$csv"
    done
  done
  rm -rf "$parts"
  echo; echo "==== $axis sweep (SUITE=$SUITE, JOBS=$JOBS) ===="
  column -t -s, "$csv"
  echo "results -> $csv"
}
