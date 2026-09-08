#!/usr/bin/env bash
#
# run_octopus.sh -- launch an Octopus benchmark suite under a chosen coherence protocol.
#
# Usage:
#   ./run_octopus.sh --protocol <snoop|directory> --suite <eembc|splash> [options]
#
# Options:
#   --protocol <snoop|directory>  Which coherence config to run (required).
#   --suite    <eembc|splash>     Which benchmark suite to run (required).
#   --bench    <name>             Run only this one benchmark (dir name under the suite).
#   --jobs     <N>                Max benchmarks to run concurrently (default: 4).
#   --safety   <seconds>          Per-benchmark wall-clock cap (default: 36000).
#   --out      <dir>              Results dir (default: results/<protocol>-<suite>).
#   -h|--help                     Show this help.
#
# Protocol selection copies configuration/SystemConfigurations/MultiCoreSystem_<Proto>.csv
# onto the active MultiCoreSystem.csv (the file the -s MultiCoreSystem class loads).
#
# Completion is judged rigorously PER CORE: each core's LatencyReport must cover its
# OWN trace (SPLASH distributes work unevenly across cores) AND carry the end-of-sim
# "Average Latency" summary footer (only written when that CPU drained its trace).
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT/build/Octopus_Simulator"; [ -x "$BIN.exe" ] && BIN="$BIN.exe"
CFG="$ROOT/configuration/SystemConfigurations"
MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
[ -d "$MINGW" ] && export PATH="$MINGW:$ROOT/build:$PATH"

PROTO="" SUITE="" ONEBENCH="" JOBS=4 SAFETY=36000 OUT=""
usage(){ sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --protocol) PROTO="${2,,}"; shift 2;;
    --suite)    SUITE="${2,,}"; shift 2;;
    --bench)    ONEBENCH="$2"; shift 2;;
    --jobs)     JOBS="$2"; shift 2;;
    --safety)   SAFETY="$2"; shift 2;;
    --out)      OUT="$2"; shift 2;;
    -h|--help)  usage 0;;
    *) echo "Unknown option: $1" >&2; usage 1;;
  esac
done

# ---- validate + select config ----
case "$PROTO" in
  snoop)     PRESET="$CFG/MultiCoreSystem_Snoop.csv";;
  directory) PRESET="$CFG/MultiCoreSystem_Directory.csv";;
  *) echo "ERROR: --protocol must be 'snoop' or 'directory'" >&2; usage 1;;
esac
case "$SUITE" in
  eembc)  TR="$ROOT/BMs/eembc-traces";;
  splash) TR="$ROOT/BMs/splash";;
  *) echo "ERROR: --suite must be 'eembc' or 'splash'" >&2; usage 1;;
esac
[ -f "$BIN" ]    || { echo "ERROR: binary not found: $BIN (build it first)" >&2; exit 1; }
[ -f "$PRESET" ] || { echo "ERROR: preset not found: $PRESET" >&2; exit 1; }
[ -d "$TR" ]     || { echo "ERROR: trace dir not found: $TR" >&2; exit 1; }
OUT="${OUT:-$ROOT/results/${PROTO}-${SUITE}}"
mkdir -p "$OUT/rows"; : > "$OUT/progress.log"

cp "$PRESET" "$CFG/MultiCoreSystem.csv"   # activate the chosen protocol
log(){ echo "$*" | tee -a "$OUT/progress.log"; }

# ---- benchmark list ----
BENCHES=()
if [ -n "$ONEBENCH" ]; then
  BENCHES=("$ONEBENCH")
else
  for d in "$TR"/*/; do [ -f "${d}trace_C0.trc.shared" ] && BENCHES+=("$(basename "$d")"); done
fi
[ ${#BENCHES[@]} -gt 0 ] || { echo "ERROR: no benchmarks with trace_C0.trc.shared in $TR" >&2; exit 1; }

run_one(){
  local b="$1" wp="$TR/$1"
  [ -f "$wp/trace_C0.trc.shared" ] || { log "[skip ] $b (no trace)"; echo "$b,,,SKIP" > "$OUT/rows/$b.csv"; return; }
  mkdir -p "$wp/newLogger"; rm -f "$wp/newLogger"/*.csv 2>/dev/null
  local start rc wall flt done_cores ncore status
  log "[start] $b @ $(date +%H:%M:%S)"
  start=$(date +%s)
  timeout "${SAFETY}s" "$BIN" -s MultiCoreSystem -p "workload_path(s)=$(cygpath -m "$wp")/" \
      >/dev/null 2>"$OUT/rows/$b.stderr"
  rc=$?; wall=$(( $(date +%s) - start ))
  # per-core completion: each core drained its OWN trace + wrote the end-of-sim footer
  ncore=0; done_cores=0
  local c refs rows fin
  for c in 0 1 2 3; do
    [ -f "$wp/trace_C$c.trc.shared" ] || continue
    ncore=$((ncore+1))
    refs=$(wc -l < "$wp/trace_C$c.trc.shared")
    rows=$(awk 'END{print NR}' "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); rows=${rows:-0}
    fin=$(grep -c "Average Latency" "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); fin=${fin:-0}
    { [ "$fin" -ge 1 ] && [ "$rows" -ge "$refs" ]; } && done_cores=$((done_cores+1))
  done
  flt=$(grep -aoE "Fault Transaction|Invalid Message|bad_function|Wrong destination|full buffer" \
        "$OUT/rows/$b.stderr" 2>/dev/null | sort -u | tr '\n' '|')
  if   [ -n "$flt" ];              then status="FAULT:$flt"
  elif [ "$rc" -eq 124 ];          then status="SAFETY_TIMEOUT"
  elif [ "$done_cores" -eq "$ncore" ] && [ "$ncore" -gt 0 ]; then status="COMPLETE"
  else status="incomplete(rc=$rc,cores=$done_cores/$ncore)"; fi
  echo "$b,$wall,$done_cores/$ncore,$status" > "$OUT/rows/$b.csv"
  log "[done ] $b wall=${wall}s cores=$done_cores/$ncore => $status @ $(date +%H:%M:%S)"
}

log "==== Octopus | protocol=$PROTO suite=$SUITE | ${#BENCHES[@]} bench, jobs=$JOBS, safety=${SAFETY}s @ $(date +%H:%M:%S) ===="
log "     config: $(basename "$PRESET") -> MultiCoreSystem.csv"
# concurrency-limited launch
active=0
for b in "${BENCHES[@]}"; do
  run_one "$b" &
  active=$((active+1))
  if [ "$active" -ge "$JOBS" ]; then wait -n 2>/dev/null || wait; active=$((active-1)); fi
done
wait

log ""; log "==== SUMMARY (protocol=$PROTO suite=$SUITE) ===="
{ echo "benchmark,wall_s,cores_done,status"; for b in "${BENCHES[@]}"; do cat "$OUT/rows/$b.csv" 2>/dev/null; done; } \
  | tee "$OUT/summary.csv" | column -t -s, | tee -a "$OUT/progress.log"
npass=$(grep -c ",COMPLETE$" "$OUT/summary.csv" 2>/dev/null)
log ""; log "PASS: $npass / ${#BENCHES[@]} complete."
