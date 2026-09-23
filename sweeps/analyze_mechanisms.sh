#!/usr/bin/env bash
# analyze_mechanisms.sh -- aggregate the Logger's per-request mechanism trackers over the
# runs of sweep_pcc_par.sh, one line per (cell, benchmark). Everything comes from the
# LatencyReport rows (cols, 1-indexed): 5 L1s 6 ReqB 7 L2s 8 L2a 9 Resp 10 L2DRAMBus 11 DRAM
# 12 L1a 13 Total 14 Eff 15 Oldest 16 LLC-arrival-state 17 LLC-gate 18 RespAhead
# 19 RespAheadRefills 20 ArrayAhead 21 ArrayAheadWrites 22 LLC-stall-state (state at the FIRST
# NonReady verdict at the LLC; -2 = never stalled by the FSM, so L2s>0 with -2 = per-line gate only)
# (docs/Logger.md S5).
#
# Request classes (by what the LLC saw when the request arrived, MESI_LLC state ids):
#   L1hit      never reached the LLC (state -2)
#   stable     line stable at arrival: I=3 S=4 EorM=5 (+ N=0 absent -> that is a miss)
#   transient  coherence transient, no DRAM: IorS_a=6 MN_d=7 S_d=8 I_d=9 N_a=10
#   coalesced  a DRAM fetch for the line already in flight: NE_d=1 NM_d=2 (DRAM=0 on own row)
#   miss       DRAM>0 or L2DRAMBus>0 on the row
# Per class: count, share, wc Total, wc Oldest (head-of-queue), wc L2s, wc L2a, wc Resp.
# Per oldest stable hit / oldest miss: younger own responses granted ahead (mean/max, refills)
# and accesses served ahead at the LLC array port (mean/max, writes).
#
# Usage: bash sweeps/analyze_mechanisms.sh eembc|splash [wl_dir]   -> results/pcc_par/mechanisms_<suite>.csv
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUITE="${1:?eembc|splash}"; WL="${2:-$ROOT/results/pcc_par/wl_$SUITE}"; OUT="$ROOT/results/pcc_par/mechanisms_$SUITE.csv"
echo "cell,bench,rows,L1hit,stable,transient,coalesced,miss,stable_wcTot,stable_wcOld,stable_wcL2s,stable_wcL2a,stable_wcResp,transient_wcTot,transient_wcOld,transient_wcL2s,coalesced_wcTot,coalesced_wcOld,coalesced_wcL2s,miss_wcTot,miss_wcOld,miss_wcResp,miss_wcDRAM,oldStable_respAhead_mean,oldStable_respAhead_max,oldStable_refillsAhead_mean,oldStable_arrayAhead_mean,oldStable_arrayAhead_max,oldStable_arrayWrites_mean,oldMiss_respAhead_mean,oldMiss_respAhead_max,neverOldest_pct,stall_S_d_n,stall_S_d_wcL2s,stall_I_d_n,stall_I_d_wcL2s,stall_MN_d_n,stall_MN_d_wcL2s,stall_IorS_a_n,stall_IorS_a_wcL2s,stall_N_a_n,stall_N_a_wcL2s,stall_fetch_n,stall_fetch_wcL2s,stall_gateonly_n,stall_gateonly_wcL2s" > "$OUT"
for cd in "$WL"/*/; do cell=$(basename "$cd"); for bd in "$cd"*/; do b=$(basename "$bd"); [ -f "$bd/newLogger/LatencyReport_C0.csv" ] || continue
  head -1 "$bd/newLogger/LatencyReport_C0.csv" | grep -q "LLC Stall State" || { echo "skip $cell/$b: rows predate the trackers" >&2; continue; }
  awk -F, -v cell="${cell/_/:}" -v b="$b" '
    FNR==1{next} NF<21||$13!~/^[0-9]+$/{next}
    { n++; if($15==0) nz++
      miss=($11>0||$10>0); st=$16
      if(miss) c="miss"; else if(st==-2) c="L1hit"; else if(st==1||st==2) c="coalesced"; else if(st>=6&&st<=10) c="transient"; else c="stable"
      N[c]++; if($13>T[c])T[c]=$13; if($15>O[c])O[c]=$15; if($7>S[c])S[c]=$7; if($8>A[c])A[c]=$8; if($9>R[c])R[c]=$9; if($11>D[c])D[c]=$11
      if($15>0 && c=="stable" && $6>0){ os++; ra+=$18; if($18>ram)ram=$18; rr+=$19; aa+=$20; if($20>aam)aam=$20; aw+=$21 }
      if($15>0 && c=="miss"){ om++; mra+=$18; if($18>mram)mram=$18 }
      if(NF>=22){ ss=$22; if(ss==8)k="S_d"; else if(ss==9)k="I_d"; else if(ss==7)k="MN_d"; else if(ss==6)k="IorS_a"; else if(ss==10)k="N_a"; else if(ss==1||ss==2)k="fetch"; else if(ss==-2&&$7>1&&$6>0)k="gate"; else k=""
        if(k!=""){ SN[k]++; if($7>SW[k])SW[k]=$7 } } }
    END{ printf "%s,%s,%d,%d,%d,%d,%d,%d,", cell, b, n, N["L1hit"]+0, N["stable"]+0, N["transient"]+0, N["coalesced"]+0, N["miss"]+0
         printf "%d,%d,%d,%d,%d,", T["stable"]+0, O["stable"]+0, S["stable"]+0, A["stable"]+0, R["stable"]+0
         printf "%d,%d,%d,", T["transient"]+0, O["transient"]+0, S["transient"]+0
         printf "%d,%d,%d,", T["coalesced"]+0, O["coalesced"]+0, S["coalesced"]+0
         printf "%d,%d,%d,%d,", T["miss"]+0, O["miss"]+0, R["miss"]+0, D["miss"]+0
         printf "%.2f,%d,%.2f,%.1f,%d,%.1f,", os?ra/os:0, ram+0, os?rr/os:0, os?aa/os:0, aam+0, os?aw/os:0
         printf "%.2f,%d,%.1f,", om?mra/om:0, mram+0, n?100*nz/n:0
         printf "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", SN["S_d"]+0,SW["S_d"]+0,SN["I_d"]+0,SW["I_d"]+0,SN["MN_d"]+0,SW["MN_d"]+0,SN["IorS_a"]+0,SW["IorS_a"]+0,SN["N_a"]+0,SW["N_a"]+0,SN["fetch"]+0,SW["fetch"]+0,SN["gate"]+0,SW["gate"]+0 }' "$bd"/newLogger/LatencyReport_C*.csv >> "$OUT"
done; done
sort -t, -k2,2 -k1,1 -o "$OUT.tmp" <(tail -n +2 "$OUT") && { head -1 "$OUT"; cat "$OUT.tmp"; } > "$OUT.2" && mv "$OUT.2" "$OUT" && rm -f "$OUT.tmp"
column -t -s, "$OUT" | cut -c1-200; echo "-> $OUT"
