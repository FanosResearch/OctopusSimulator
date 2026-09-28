#!/usr/bin/env bash
# analyze_pcc_ab.sh -- tabulate the perfect-vs-real LLC A/B produced by sweep_pcc_ab.sh.
#
# Per benchmark it reports, side by side:
#   PERFECT  : worst-case per stage from results/pcc_ab/<suite>.csv (value=1). Under a
#              perfect LLC every request is a hit, so this IS the hit worst-case.
#   REAL-HIT : worst-case per stage over the real run's HITS only (DRAM==0 rows of the
#              surviving per-request LatencyReport_C*.csv, value=0 -- the last run).
#   REAL-ALL : the real run's lumped worst-case (from the csv), i.e. incl. misses.
# and the deltas that test the timing analysis (timing_analysis_PCC_RR.md):
#   dHitTotal = REAL-HIT wcTotal - PERFECT wcTotal   (how much DRAM leaks into a HIT)
#   RR check  : REAL-HIT wcRespBus should stay <= (C-1)*Lacc + Lacc = 25 under RR (C = 5
#               arbiter candidates: 4 L1s + LLC; the Logger's Response-Bus includes the
#               message's own 5-cycle transfer slot) -- meaningful at OoO=1 (head-of-queue)
#   refill    : REAL-HIT wcL2stall/wcL2access growth over PERFECT (the array-side term)
#
# Usage: bash sweeps/analyze_pcc_ab.sh eembc|splash [ROWS_DIR]
#   ROWS_DIR = where the real run's newLogger dirs live (default: the BMs suite dir;
#              pass results/pcc_ab/rows/<suite> if they were archived there).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUITE="${1:?eembc|splash}"; CSV="$ROOT/results/pcc_ab/$SUITE.csv"
case "$SUITE" in eembc) TR="$ROOT/BMs/eembc-traces";; splash) TR="$ROOT/BMs/splash";; *) echo "bad suite" >&2; exit 1;; esac
ROWS="${2:-$TR}"
[ -f "$CSV" ] || { echo "no $CSV yet" >&2; exit 1; }

# hit-only worst case from rows: cols 5 L1s,6 ReqB,7 L2s,8 L2a,9 RespB,12 L1Acc,13 Total,14 Eff; hit <=> $11==0
hitwc(){ awk -F, 'FNR==1{next} NF>=14 && $13 ~ /^[0-9]+$/ && $11==0 {
           n++; if($13>wt)wt=$13; if($7>l2s)l2s=$7; if($8>l2a)l2a=$8; if($9>rp)rp=$9; if($6>rq)rq=$6 }
         END{ if(n) printf "%d,%d,%d,%d,%d,%d", wt, l2s, l2a, rp, rq, n; else printf "NA,NA,NA,NA,NA,0" }' \
         "$1"/LatencyReport_C*.csv 2>/dev/null; }

printf "%-16s | %-30s | %-30s | %-16s | %8s %8s %6s\n" "benchmark" "PERFECT wc: Tot L2s L2a Resp" "REAL-HIT wc: Tot L2s L2a Resp" "REAL-ALL Tot DRAM" "dHitTot" "dHitL2" "RR<=25"
printf '%s\n' "-------------------------------------------------------------------------------------------------------------------------------"
# csv cols: 1 value,2 bench,3 status,4 avg,5 wcTotal,6 wcEff,7 wcL1s,8 wcReqBus,9 wcL2s,10 wcL2a,11 wcResp,12 wcDramBus,13 wcDRAM,14 wcL1access,15 finish
awk -F, 'NR>1{ k=$2; if($1=="1"){P[k]=$0} else if($1=="0"){R[k]=$0} } END{ for(k in P) print k }' "$CSV" | sort | while read -r b; do
  p=$(awk -F, -v b="$b" '$1=="1"&&$2==b' "$CSV"); r=$(awk -F, -v b="$b" '$1=="0"&&$2==b' "$CSV")
  pT=$(echo "$p"|cut -d, -f5); pL2s=$(echo "$p"|cut -d, -f9); pL2a=$(echo "$p"|cut -d, -f10); pR=$(echo "$p"|cut -d, -f11); pst=$(echo "$p"|cut -d, -f3)
  rT=$(echo "$r"|cut -d, -f5); rD=$(echo "$r"|cut -d, -f13); rst=$(echo "$r"|cut -d, -f3)
  IFS=, read -r hT hL2s hL2a hR hRq hn <<< "$(hitwc "$ROWS/$b/newLogger")"
  if [ "$hT" != "NA" ] && [ "$pT" != "NA" ]; then dT=$((hT-pT)); dL2=$(( (hL2s+hL2a) - (pL2s+pL2a) )); else dT=NA; dL2=NA; fi
  rr=$([ "$hR" != "NA" ] && { [ "$hR" -le 25 ] && echo "ok" || echo "VIOL"; } || echo "NA")
  printf "%-16s | %5s %5s %5s %5s (%s) | %5s %5s %5s %5s (%s,n=%s) | %7s %6s | %8s %8s %6s\n" \
    "$b" "$pT" "$pL2s" "$pL2a" "$pR" "${pst:0:4}" "$hT" "$hL2s" "$hL2a" "$hR" "${rst:0:4}" "$hn" "$rT" "$rD" "$dT" "$dL2" "$rr"
done
