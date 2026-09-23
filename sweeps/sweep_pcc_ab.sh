#!/usr/bin/env bash
# sweep_pcc_ab.sh -- the perfect-vs-real LLC A/B under a PCC-like predictable setup.
#
# Config (held fixed): snoop MESI + Exclusive L1 (gen_baseline), ROUND-ROBIN on the
# request bus AND the response bus (set_arbiter RRArbiter: one policy for both
# channels), ROUND-ROBIN on the LLC data-array arbiter, and MCsim as main memory
# with its FRFCFS scheduler (system/FRFCFS/FRFCFS.ini): a cycle-accurate DDR model
# with real bank parallelism. This mimics "PCC under RR" (ECRTS'22) with a realistic
# DRAM behind it. (MCsim's Round scheduler was found to stall once misses begin.)
#
# Axis (the ONLY difference between the two runs): llc_controller.perfect_llc
#   1 -> perfect LLC: every access hits, memory is never touched (the assumption
#        every predictable-coherence bound makes; MCsim is configured but idle)
#   0 -> real LLC: misses go to MCsim/Round
#
# Metrics: per-stage worst-case + mean, computed from the per-request
# LatencyReport_C*.csv rows rather than Summary.csv, so wall-clock-CAPPED runs
# (SPLASH giants, SPLASH+MCsim) still yield valid numbers -- the Logger flushes
# rows incrementally and never reaches traceEnd on a timeout. For complete runs
# this equals the Summary.csv footer. 'finish' is taken from Summary.csv when the
# run completed, else NA.
#
# Usage:  SUITE=eembc  SAFETY=1800 bash sweeps/sweep_pcc_ab.sh
#         SUITE=splash SAFETY=7200 bash sweeps/sweep_pcc_ab.sh
# Output: results/pcc_ab/<suite>.csv  (value column = perfect_llc)
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

VALUES="1 0"
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"
trap 'cp "$_sbc_bak" "$SBC"; rm -f "$_sbc_bak"' EXIT   # always restore the arbiter file

# Row-based metrics (see header). Columns of LatencyReport (1-indexed):
# 4 CPU, 5 L1Stall, 6 ReqBus, 7 L2Stall, 8 L2Access, 9 RespBus, 10 L2DRAMBus,
# 11 DRAM, 12 L1Access, 13 Total, 14 Effective, 15 Oldest (head-of-queue). Data rows have NF>=14 and a
# numeric Total; the footer rows have fewer fields and are skipped.
metrics(){
  local dir; dir="$(dirname "$1")"
  local fin="NA"
  [ -f "$1" ] && fin=$(awk -F, 'NR>1&&NF>=13&&$13+0>f{f=$13} END{print (f?f:"NA")}' "$1")
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

apply_value(){
  gen_baseline                                   # snoop MESI + CacheControllerExclusive
  set_arbiter RRArbiter                          # RR on request AND response bus
  set_csv 'llc_controller\.arbiter_type\(s\),'  "llc_controller.arbiter_type(s),RRArbiter,,,,,"
  set_csv 'main_memory_type\(s\),'              "main_memory_type(s),MCsim,,,,,"
  set_csv 'mcsim_scheduler\(s\),'               "mcsim_scheduler(s),FRFCFS,,,,,"
  set_csv 'llc_controller\.perfect_llc\(i\),'   "llc_controller.perfect_llc(i),$1,,,,,"
}
run_axis pcc_ab perfect_llc
