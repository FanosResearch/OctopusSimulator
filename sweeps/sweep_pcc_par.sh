#!/usr/bin/env bash
# sweep_pcc_par.sh -- the perfect-vs-real LLC A/B under a PCC-like predictable setup,
# crossed with the core's outstanding-request window (OoO), with EVERY (config, bench)
# run launched IN PARALLEL.
#
# Why this can be parallel when the other sweeps cannot: the part of the config that
# differs between cells here -- llc_controller.perfect_llc and the CPU's OoO window --
# is passed as -p command-line overrides, so no config file is mutated while runs
# execute. The shared config (snoop MESI + Exclusive L1, ROUND-ROBIN on the request
# bus, the response bus and the LLC array arbiter, MCsim main memory with its FRFCFS
# scheduler system/FRFCFS/FRFCFS.ini -- a cycle-accurate DDR model with real bank
# parallelism and reordering; MCsim's Round scheduler was found to stall once misses
# begin) is written ONCE up front. Each run
# gets its own workload dir with HARDLINKED traces (free on NTFS/ext4), so the
# per-run newLogger/ outputs never collide.
#
# OoO=1 : in-order (<=1 request in flight) -> per-request latency == head-of-queue
#         latency, the quantity PCC's Lemma 8/9 bound. Apples-to-apples comparison.
# OoO=8 : the CPU default; tests Lemma 9 ("OoO leaves the bound unchanged") and
#         exposes the outstanding-responses term of the timing analysis.
#
# Metrics are computed from the per-request LatencyReport rows (valid for wall-clock
# capped runs); see sweep_pcc_ab.sh. Value column = "perfect_llc:ooo".
#
# perfect_llc values: 0 real | 1 zero-latency memory (real capacity) | 2 PCC-perfect (LLC sized
# PCC_LLC_BYTES, default 64 MiB, so nothing ever misses). Value column = "perfect_llc:ooo".
#
# Usage: SUITE=eembc  SAFETY=1800 PERFECT_VALUES="2 1 0" OOO_VALUES="1 8" bash sweeps/sweep_pcc_par.sh
#        SUITE=splash SAFETY=7200 PERFECT_VALUES="1 0" OOO_VALUES="1"   bash sweeps/sweep_pcc_par.sh
# Env: JOBS (default nproc-2), EXCLUDE, BENCH  (as sweep_common.sh)
# Output: results/pcc_par/<suite>.csv ; per-run dirs under results/pcc_par/wl_<suite>/
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

PERFECT_VALUES="${PERFECT_VALUES:-1 0}"
OOO_VALUES="${OOO_VALUES:-1 8}"
OUT="$SWEEP_ROOT/results/pcc_par"; WL="$OUT/wl_$SUITE"; PARTS="$OUT/.parts_$SUITE"
mkdir -p "$WL" "$PARTS"; rm -f "$PARTS"/*

# Restore both config files on exit (the other sweeps restore only the arbiter file;
# we also put the active system CSV back so the tree is left as we found it).
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
trap 'cp "$_sbc_bak" "$SBC"; cp "$_cfg_bak" "$CFG"; rm -f "$_sbc_bak" "$_cfg_bak"' EXIT

# --- the shared config, written once ----------------------------------------------
gen_baseline
set_arbiter RRArbiter
set_csv 'llc_controller\.arbiter_type\(s\),'  "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'              "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'               "mcsim_scheduler(s),FRFCFS,,,,,"
# perfect_llc and OoO deliberately NOT written here: they are per-run -p overrides.

# row-based metrics over a run dir's LatencyReport_C*.csv (see sweep_pcc_ab.sh)
rowmetrics(){
  local dir="$1" fin="NA"
  [ -f "$dir/Summary.csv" ] && fin=$(awk -F, 'NR>1&&NF>=13&&$13+0>f{f=$13} END{print (f?f:"NA")}' "$dir/Summary.csv")
  awk -F, -v fin="$fin" '
    FNR==1 { next }
    NF>=14 && $13 ~ /^[0-9]+$/ {
      n++; a+=$14
      if($13>wt)wt=$13; if($14>ef)ef=$14; if($5>l1)l1=$5; if($6>rq)rq=$6;
      if($7>l2s)l2s=$7; if($8>l2a)l2a=$8; if($9>rp)rp=$9; if($10>db)db=$10; if($11>dr)dr=$11; if($12>l1a)l1a=$12; if(NF>=15&&$15>ol)ol=$15
    }
    END { if(n) printf "%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d", a/n, wt, ef, l1, rq, l2s, l2a, rp, db, dr, l1a, fin, ol;
          else  printf "NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA" }' "$dir"/LatencyReport_C*.csv 2>/dev/null
}

# one (bench, perfect, ooo) run in its own hardlinked workload dir
run_one(){
  local b="$1" p="$2" o="$3"
  local src="$TR/$b" d="$WL/${p}_${o}/$b" c f
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv 2>/dev/null
  for c in 0 1 2 3; do
    f="$src/trace_C$c.trc.shared"; [ -f "$f" ] || continue
    [ -f "$d/trace_C$c.trc.shared" ] || ln -f "$f" "$d/trace_C$c.trc.shared" 2>/dev/null || cp "$f" "$d/"
  done
  local s rc flt nc=0 dn=0 refs rows fin st pflags
  # perfect_llc axis: 0 = real LLC + DRAM; 1 = zero-latency memory (real capacity, fills
  # injected next cycle: refills/evictions still happen); 2 = PCC-perfect (LLC large enough
  # for the whole trace -- Octopus pre-warms every trace line -- so NO misses, refills or
  # evictions: the assumption the SOTA bounds make; the sim aborts at start if it does not fit).
  case "$p" in
    2) pflags="-p llc_controller.perfect_llc(i)=1 -p llc_controller.m_data_handler.cache_size(i)=${PCC_LLC_BYTES:-67108864}";;
    *) pflags="-p llc_controller.perfect_llc(i)=$p";;
  esac
  s=$(date +%s)
  timeout "${SAFETY}s" "$BIN" -s MultiCoreSystem \
      -p "workload_path(s)=$(cygpath -m "$d" 2>/dev/null || echo "$d")/" \
      $pflags \
      -p "cpu[*].m_number_of_OoO_requests(i)=$o" >/dev/null 2>"$d/.stderr"
  rc=$?
  flt=$(grep -aoiE 'fault|invalid|segmentation|abort|unfound|not found|bad_function|full buffer|wrong destination' "$d/.stderr" 2>/dev/null | head -1)
  for c in 0 1 2 3; do
    [ -f "$d/trace_C$c.trc.shared" ] || continue; nc=$((nc+1))
    refs=$(wc -l < "$d/trace_C$c.trc.shared")
    rows=$(awk 'END{print NR}' "$d/newLogger/LatencyReport_C$c.csv" 2>/dev/null); rows=${rows:-0}
    fin=$(grep -c "Average Latency" "$d/newLogger/LatencyReport_C$c.csv" 2>/dev/null); fin=${fin:-0}
    { [ "$fin" -ge 1 ] && [ "$rows" -ge "$refs" ]; } && dn=$((dn+1))
  done
  if   [ -n "$flt" ];                        then st="FAULT"
  elif [ "$rc" -eq 124 ];                    then st="TIMEOUT"
  elif [ "$dn" -eq "$nc" ] && [ "$nc" -gt 0 ]; then st="OK"
  else st="INCOMPLETE"; fi
  echo "$p:$o,$b,$st,$(rowmetrics "$d/newLogger")" > "$PARTS/${p}_${o}_$b.line"
  echo "[done ] $p:$o $b $st wall=$(( $(date +%s)-s ))s"
}

# --- launch everything, throttled to JOBS ---------------------------------------
n=0
for b in $(benches); do for p in $PERFECT_VALUES; do for o in $OOO_VALUES; do
  run_one "$b" "$p" "$o" &
  n=$((n+1))
  while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n 2>/dev/null || break; done
done; done; done
echo "launched $n runs (JOBS=$JOBS, SAFETY=${SAFETY}s); waiting..."
wait

CSV="$OUT/$SUITE.csv"
{ echo "perfect_llc:ooo,benchmark,status,$METRIC_HEADER"; cat "$PARTS"/*.line | sort -t, -k2,2 -k1,1; } > "$CSV"
echo; column -t -s, "$CSV" 2>/dev/null || cat "$CSV"; echo "-> $CSV"
