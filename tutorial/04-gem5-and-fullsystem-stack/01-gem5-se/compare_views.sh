#!/usr/bin/env bash
# compare_views.sh <gem5 run directory>
#
# The same run seen from both ends: gem5's stats.txt (the bridge's per-L1
# counters, ROI dump) next to Octopus's newLogger/ reports (one LatencyReport
# per core, as in exercise 00). Run gem5 with
#   --octopus-param 'cpu[*].log_requests(i)=1'
# so Octopus writes newLogger/ for the same window as gem5's statistics.
#
# Per core: requests gem5 handed to Octopus vs rows Octopus reported (equal,
# except requests still in flight when the window closed), and the average
# latency each side measured (gem5: hand-off to response at the bridge;
# Octopus: Total Latency, CPU issue to response).
set -uo pipefail
RUN=${1:?usage: compare_views.sh <gem5 run directory>}
STATS="$RUN/stats.txt"; LOGS="$RUN/newLogger"
[ -f "$STATS" ] || { echo "no $STATS"; exit 1; }
[ -f "$LOGS/Summary.csv" ] || { echo "no $LOGS/Summary.csv: run gem5 with --octopus-param 'cpu[*].log_requests(i)=1'"; exit 1; }

# The first dump in stats.txt is the window's (m5 dumpstats / workend); gem5
# dumps once more at exit.
ROI=$(mktemp); trap 'rm -f "$ROI"' EXIT
awk '/Begin Simulation Statistics/{n++} n==1' "$STATS" > "$ROI"

printf "%-5s %14s %14s %9s   %16s %16s\n" core "gem5 requests" "Octopus rows" "in flight" "gem5 latency" "Octopus latency"
for f in "$LOGS"/LatencyReport_C*.csv; do
    c=${f##*_C}; c=${c%.csv}
    req=$(awk -v c="$c" '$1 ~ "l1[di]_caches"c"\\.(readReqs|writeReqs)$" {s+=$2} END{print s+0}' "$ROI")
    lat=$(awk -v c="$c" '$1 ~ "l1[di]_caches"c"\\.latencyCycles$" {l+=$2} $1 ~ "l1[di]_caches"c"\\.responses$" {r+=$2} END{if (r) printf "%.1f", l/r; else print "-"}' "$ROI")
    # request rows only (numeric id, full width); the trailer line is a summary
    rows=$(awk -F, 'NR>1 && NF>20 && $1 ~ /^[0-9]+$/ {n++} END{print n+0}' "$f")
    olat=$(awk -F, 'NR>1 && NF>20 && $1 ~ /^[0-9]+$/ {s+=$13; n++} END{if (n) printf "%.1f", s/n; else print "-"}' "$f")
    printf "%-5s %14s %14s %9s   %13s cy %13s cy\n" "$c" "$req" "$rows" "$((req - rows))" "$lat" "$olat"
done
echo
echo "Octopus Summary.csv (per core, worst cases; as in exercise 00):"
column -s, -t < "$LOGS/Summary.csv" | cut -c1-200
