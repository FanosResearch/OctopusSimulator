#!/usr/bin/env bash
# reduce_reports.sh <run directory> [--delete]
#
# Reduces Octopus's per-request LatencyReport_C<core>.csv (newLogger/) to one
# line per core in <run>/breakdown.csv:
#   requests            all requests the core issued in the window
#   beyond_l1           requests that used the bus (any stage past the L1)
#   dram                requests that went to DRAM (an LLC miss)
#   mean_<stage>        mean cycles per stage over the beyond-L1 requests
#   p99_total           99th percentile of Total Latency over them
# Spinning threads hit their L1 millions of times, so averages over every
# request say little; the beyond-L1 requests are where interference shows.
# --delete removes the raw LatencyReport files afterwards (Summary.csv stays).
set -uo pipefail
RUN=${1:?usage: reduce_reports.sh <run directory> [--delete]}
LOGS="$RUN/newLogger"
ls "$LOGS"/LatencyReport_C*.csv > /dev/null 2>&1 || { echo "no $LOGS/LatencyReport_C*.csv"; exit 1; }

OUT="$RUN/breakdown.csv"
echo "core,requests,beyond_l1,dram,mean_l1_stall,mean_req_bus,mean_l2_stall,mean_l2_access,mean_resp_bus,mean_l2_dram_bus,mean_dram,mean_l1_access,mean_total,p99_total" > "$OUT"
for f in "$LOGS"/LatencyReport_C*.csv; do
    c=${f##*_C}; c=${c%.csv}
    # columns: 5 L1 stall, 6 request bus, 7 L2 stall, 8 L2 access, 9 response
    # bus, 10 L2-DRAM bus, 11 DRAM, 12 L1 access, 13 total
    awk -F, -v core="$c" '
        NR > 1 && NF > 20 && $1 ~ /^[0-9]+$/ {
            n++
            past = $6 + $7 + $8 + $9 + $10 + $11
            if (past > 0) {
                b++
                for (i = 5; i <= 13; i++) s[i] += $i
                t = $13 > 100000 ? 100000 : $13; h[t]++
                if ($11 > 0) d++
            }
        }
        END {
            printf "%s,%d,%d,%d", core, n, b, d
            for (i = 5; i <= 13; i++) printf ",%.2f", b ? s[i] / b : 0
            k = 0; p99 = 0
            for (t = 0; t <= 100000; t++) { k += h[t]; if (k >= 0.99 * b) { p99 = t; break } }
            printf ",%d\n", p99
        }' "$f" >> "$OUT"
done
column -s, -t "$OUT"
if [ "${2:-}" = "--delete" ]; then
    rm -f "$LOGS"/LatencyReport_C*.csv
fi
