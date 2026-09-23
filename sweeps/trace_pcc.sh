#!/usr/bin/env bash
# trace_pcc.sh -- the PCC-like cells of sweep_pcc_par.sh (same shared config: snoop MESI +
# Exclusive L1, RR on request bus / response bus / LLC array, MCsim FRFCFS), re-run WITH the
# raw event trace (OCTOPUS_TRACE, docs/Trace.md) and converted for the visualizer and the
# trace-based analyses (sweeps/analyze_trace_ahead.py: what really sat ahead of an oldest hit
# at the LLC array port and on the response bus -- write-backs, fills, other cores' responses).
#
# The config is written the way the sweep writes it (gen_baseline + the same set_csv lines)
# and restored on exit, so a traced run is bit-identical to the sweep's cell (the simulator is
# deterministic). perfect_llc / OoO are -p overrides, so cells run in parallel.
#
# Usage: SUITE=eembc PERFECT_VALUES="2 0" OOO_VALUES="8 1" JOBS=4 bash sweeps/trace_pcc.sh
# Env:   BENCH=<name> (one bench), EXCLUDE, SAFETY (s, default 1800), KEEP_TRACE=1 (keep the
#        binary trace; default: deleted once converted -- a2time01 is 100 MB, SPLASH GBs),
#        TRACE_WINDOW=t0:t1 (OCTOPUS_TRACE_WINDOW: trace only that cycle range; the
#        conversion is then validated on that window), PART_ROWS (Parquet part size).
# Output: results/pcc_par/wl_trace_<suite>/<perfect>_<ooo>/<bench>/newLogger/
#           octoviz.parquet + occupancy[_NNN].parquet + fsm[_NNN].parquet  (viewer + analyses)
#         results/pcc_par/wl_trace_<suite>/<perfect>_<ooo>/<bench>/.convert.log  (validation)
#         results/pcc_par/trace_<suite>.csv  (status, wall, trace size, validation mismatches)
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

PERFECT_VALUES="${PERFECT_VALUES:-2 0}"
OOO_VALUES="${OOO_VALUES:-8 1}"
SAFETY="${SAFETY:-1800}"
KEEP_TRACE="${KEEP_TRACE:-0}"
TRACE_WINDOW="${TRACE_WINDOW:-}"
PART_ROWS="${PART_ROWS:-2000000}"
PY="${PY:-python}"
OUT="$SWEEP_ROOT/results/pcc_par"; WL="$OUT/wl_trace_$SUITE"; STATUS="$OUT/trace_$SUITE.csv"
mkdir -p "$WL"

# restore both config files on exit (as sweep_pcc_par.sh does)
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
trap 'cp "$_sbc_bak" "$SBC"; cp "$_cfg_bak" "$CFG"; rm -f "$_sbc_bak" "$_cfg_bak"' EXIT

# --- the shared config, written once (identical to sweep_pcc_par.sh) -----------------
gen_baseline
set_arbiter RRArbiter
set_csv 'llc_controller\.arbiter_type\(s\),'  "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'              "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'               "mcsim_scheduler(s),FRFCFS,,,,,"

run_one(){
  local b="$1" p="$2" o="$3"
  local src="$TR/$b" d="$WL/${p}_${o}/$b" c pflags s rc st tr sz mism
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv "$d/newLogger"/*.parquet "$d/trace.bin" "$d/trace.bin.names" 2>/dev/null
  for c in 0 1 2 3; do
    [ -f "$src/trace_C$c.trc.shared" ] || continue
    [ -f "$d/trace_C$c.trc.shared" ] || ln -f "$src/trace_C$c.trc.shared" "$d/trace_C$c.trc.shared" 2>/dev/null || cp "$src/trace_C$c.trc.shared" "$d/"
  done
  case "$p" in
    2) pflags="-p llc_controller.perfect_llc(i)=1 -p llc_controller.m_data_handler.cache_size(i)=${PCC_LLC_BYTES:-67108864}";;
    *) pflags="-p llc_controller.perfect_llc(i)=$p";;
  esac
  tr="$(cygpath -m "$d" 2>/dev/null || echo "$d")/trace.bin"
  s=$(date +%s)
  OCTOPUS_TRACE="$tr" OCTOPUS_TRACE_WINDOW="$TRACE_WINDOW" timeout "${SAFETY}s" "$BIN" -s MultiCoreSystem \
      -p "workload_path(s)=$(cygpath -m "$d" 2>/dev/null || echo "$d")/" \
      $pflags \
      -p "cpu[*].m_number_of_OoO_requests(i)=$o" >/dev/null 2>"$d/.stderr"
  rc=$?
  sz=$(stat -c %s "$d/trace.bin" 2>/dev/null || echo 0)
  if   [ "$rc" -eq 124 ]; then st="TIMEOUT"
  elif [ "$rc" -ne 0 ] || ! grep -q "Average Latency" "$d/newLogger/LatencyReport_C0.csv" 2>/dev/null; then st="FAIL"
  else
    local win=""
    [ -n "$TRACE_WINDOW" ] && win="--t0 ${TRACE_WINDOW%%:*} --t1 ${TRACE_WINDOW##*:}"
    if $PY "$SWEEP_ROOT/tools/octoviz/convert.py" "$d/newLogger" --trace "$d/trace.bin" --part-rows "$PART_ROWS" $win > "$d/.convert.log" 2>&1; then
      st="OK"; [ "$KEEP_TRACE" = "1" ] || rm -f "$d/trace.bin"
    else st="CONVERT_FAIL"; fi
  fi
  mism=$(awk '/mismatches=/{split($0,a,"mismatches="); m+=a[2]} /double-booking:/{db=($0 ~ /none/)?0:1} END{printf "%d,%d", m+0, db+0}' "$d/.convert.log" 2>/dev/null)
  echo "${p}:${o},${b},${st},$(( $(date +%s) - s )),${sz},${mism:-NA,NA}" >> "$STATUS"
  printf '[done ] %s:%s %-16s %-12s wall=%ss trace=%s B  mismatches,dbl=%s\n' "$p" "$o" "$b" "$st" "$(( $(date +%s) - s ))" "$sz" "${mism:-NA}"
}

echo "cell,benchmark,status,wall_s,trace_bytes,row_mismatches,double_booking" > "$STATUS"
benches=()
for src in "$TR"/*/; do
  b="$(basename "$src")"; [ -f "$src/trace_C0.trc.shared" ] || continue
  [ -n "${BENCH:-}" ] && [ "$b" != "$BENCH" ] && continue
  case " $EXCLUDE " in *" $b "*) continue;; esac
  benches+=("$b")
done
echo "=== $(date) trace_pcc suite=$SUITE cells=[$PERFECT_VALUES] x [$OOO_VALUES] benches=${#benches[@]} jobs=$JOBS ==="
n=0
for p in $PERFECT_VALUES; do for o in $OOO_VALUES; do for b in "${benches[@]}"; do
  run_one "$b" "$p" "$o" &
  n=$((n+1)); [ "$n" -ge "$JOBS" ] && { wait -n; n=$((n-1)); }
done; done; done
wait
echo "=== $(date) DONE -> $STATUS ==="
sort -t, -k1,1 -k2,2 "$STATUS" | column -t -s, 2>/dev/null || cat "$STATUS"
