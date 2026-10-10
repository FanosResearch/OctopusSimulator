#!/usr/bin/env bash
# analyze_pcc_par.sh -- tabulate the perfect-vs-real x OoO A/B produced by sweep_pcc_par.sh.
#
# Every (perfect_llc, ooo, bench) run kept its own per-request rows under
# results/pcc_par/wl_<suite>/<p>_<o>/<bench>/newLogger/, so for BOTH the perfect and the
# real cell we report the worst case over HITS ONLY (rows with DRAM==0) -- the quantity
# the timing analysis bounds -- next to the lumped (hits+misses) worst case from the csv.
#   dHitTot/dHitL2/dHitResp = REAL-hit minus PERFECT-hit: how much DRAM leaks into a HIT,
#                             and which stage absorbs it (analysis: array stages under RR)
#   RR<=25 : REAL-hit wcRespBus vs the per-core-RR bound (C-1)*Lacc + Lacc = 25: the
#            Logger's Response-Bus = RESP_BUS.EXIT - responder.EXIT and the bus stamps
#            EXIT at the END of the 5-cycle transfer slot, so the wait-for-grant bound
#            (C-1)*Lacc = 20 carries the message's own slot on top. Meaningful at
#            ooo=1 (head-of-queue == per-request); a write-back of the same core
#            queued ahead in the same owner slot adds one more round (C*Lacc).
# Usage: bash sweeps/analyze_pcc_par.sh eembc|splash
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUITE="${1:?eembc|splash}"; OUT="$ROOT/results/pcc_par"; CSV="$OUT/$SUITE.csv"; WL="$OUT/wl_$SUITE"
[ -f "$CSV" ] || { echo "no $CSV yet" >&2; exit 1; }

# hit-only worst case: cols 6 ReqB,7 L2s,8 L2a,9 RespB,12 L1Acc,13 Total; hit <=> $11==0 && $10==0
# (csv cols: 1 value,2 bench,3 status,4 avg,5 wcTotal,...,13 wcDRAM,14 wcL1access,15 finish)
hitwc(){ awk -F, 'FNR==1{next} NF>=14 && $13 ~ /^[0-9]+$/ && $11==0 && $10==0 {
           n++; if($13>wt)wt=$13; if($7>l2s)l2s=$7; if($8>l2a)l2a=$8; if($9>rp)rp=$9 }
         END{ if(n) printf "%d,%d,%d,%d,%d", wt, l2s, l2a, rp, n; else printf "NA,NA,NA,NA,0" }' \
         "$1"/LatencyReport_C*.csv 2>/dev/null; }
field(){ awk -F, -v v="$1" -v b="$2" -v c="$3" '$1==v&&$2==b{print $c; exit}' "$CSV"; }

printf "%-14s %-3s | %-24s | %-30s | %-13s | %7s %7s %8s %6s\n" "benchmark" "ooo" "PERFECT-hit Tot L2s L2a Rsp" "REAL-hit Tot L2s L2a Rsp (n)" "REAL-all Tot DRAM" "dHitTot" "dHitL2" "dHitResp" "RR<=25"
printf '%s\n' "----------------------------------------------------------------------------------------------------------------------------------"
awk -F, 'NR>1{split($1,v,":"); print $2, v[2]}' "$CSV" | sort -u | while read -r b o; do
  IFS=, read -r pT pL2s pL2a pR pn <<< "$(hitwc "$WL/1_$o/$b/newLogger")"
  IFS=, read -r hT hL2s hL2a hR hn <<< "$(hitwc "$WL/0_$o/$b/newLogger")"
  rT=$(field "0:$o" "$b" 5); rD=$(field "0:$o" "$b" 13); rst=$(field "0:$o" "$b" 3); pst=$(field "1:$o" "$b" 3)
  if [ "${hT:-NA}" != "NA" ] && [ "${pT:-NA}" != "NA" ]; then dT=$((hT-pT)); dL2=$(( (hL2s+hL2a)-(pL2s+pL2a) )); dR=$((hR-pR)); else dT=NA; dL2=NA; dR=NA; fi
  rr=$([ "${hR:-NA}" != "NA" ] && { [ "$hR" -le 25 ] && echo ok || echo VIOL; } || echo NA)
  printf "%-14s %-3s | %5s %5s %5s %5s (%s) | %5s %5s %5s %5s (%s,%s) | %6s %6s | %7s %7s %8s %6s\n" \
    "$b" "$o" "$pT" "$pL2s" "$pL2a" "$pR" "${pst:0:4}" "$hT" "$hL2s" "$hL2a" "$hR" "${rst:0:4}" "$hn" "${rT:-NA}" "${rD:-NA}" "$dT" "$dL2" "$dR" "$rr"
done
