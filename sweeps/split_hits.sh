#!/usr/bin/env bash
# split_hits.sh -- classify a run's HITS into independent vs coalesced and report the
# worst case of each.
#
#   coalesced hit  = a request to the SAME 64-B line as a miss whose [issue, complete]
#                    window overlaps the request's own window: the line is in flight for
#                    someone else, the request waits for that fill (its L2-Stall is one
#                    DRAM excursion) and is then served from the array, so its own row
#                    shows DRAM = 0 and the Logger counts it as a hit (hit-under-miss).
#   independent hit = every other hit: the line was resident; any delay it suffers is
#                    genuine arbitration interference (LLC queue/array, response bus).
#
# Time base: a row's issue cycle is Ready Cycle + CPU Latency (col 3 + col 4), exact
# since the Logger reports the READY cycle (2026-09-21). Reports written before that
# carried the raw trace timestamp in col 3, which is unrelated to sim time, so this
# classifier is only valid on reports with the "Ready Cycle" header.
#
# Sort-merge over all cores' per-request rows (linear after one sort, so it scales to
# the SPLASH giants): events (line, start, end, kind, stages) sorted by line then
# start; a forward pass catches misses that started before the hit and are still
# running (max miss end per line), a backward pass catches misses that start inside
# the hit's window (min miss start per line).
#
# Usage: bash sweeps/split_hits.sh <newLogger dir>
# Prints: hits coalesced | indep wc Tot L2s L2a Resp | coalesced wc Tot L2s
set -u
d="${1:?newLogger dir}"; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
head -1 "$d"/LatencyReport_C0.csv | grep -q "Ready Cycle" || { echo "split_hits: $d has pre-Ready-Cycle rows (trace timestamps) -- re-run the simulation" >&2; exit 1; }
awk -F, 'FNR==1{next} NF<14||$13!~/^[0-9]+$/{next}
 { key=int(strtonum("0x"$2)/64); st=$3+$4; en=st+$13; ty=($11>0||$10>0)?"M":"H"
   print key, st, en, ty, $13, $7, $8, $9 }' "$d"/LatencyReport_C*.csv \
 | LC_ALL=C sort -S1G -k1,1n -k2,2n > "$t/ev"
awk '{ if($4=="M"){ if($3>mx[$1]) mx[$1]=$3 } else print (mx[$1]>=$2)?1:0 }' "$t/ev" > "$t/f"
tac "$t/ev" | awk '{ if($4=="M"){ if(!($1 in mn)||$2<mn[$1]) mn[$1]=$2 } else print (($1 in mn)&&mn[$1]<=$3)?1:0 }' | tac > "$t/b"
grep -v " M " "$t/ev" | paste -d' ' - "$t/f" "$t/b" | awk '
 { nh++; if($9||$10){ nc++; if($5>cT)cT=$5; if($6>cS)cS=$6 }
   else { if($5>iT)iT=$5; if($6>iS)iS=$6; if($7>iA)iA=$7; if($8>iR)iR=$8 } }
 END{ printf "%9d %9d | %5d %5d %5d %5d | %5d %5d\n", nh, nc+0, iT+0, iS+0, iA+0, iR+0, cT+0, cS+0 }'
