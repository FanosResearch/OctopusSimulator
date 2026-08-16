#!/bin/bash
#
# Drive the full 30-run experimental matrix for the R2 2D-mesh evaluation:
#
#   topologies (2)        : mesh2d, fullyconnected
#   core counts (5)       : 2 (2x1), 4 (2x2), 8 (4x2), 16 (4x4), 32 (8x4)
#   strategies (3)        : antipodal, neighbor, random   (driven inside run_pingpong_strategies.sh)
#   protocol              : MESI
#   iters per core        : 10000
#
# Writes one results file per (topology, cores) combo to results/, with the
# WCL min/max/spread/finish for each strategy.
#
# Usage:  ./run_paper_sweep.sh [iters]    (default 10000)

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

iters=${1:-10000}
RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"

# (cores, rows) pairs. cols is derived as cores/rows in the harness.
# 2 -> 2x1, 4 -> 2x2, 8 -> 4x2, 16 -> 4x4, 32 -> 8x4
declare -a CONFIGS=(
    "2  2"
    "4  2"
    "8  4"
    "16 4"
    "32 8"
)

TOPOLOGIES=(mesh2d fullyconnected)
PROTOCOL=MESI

echo "Paper sweep: $iters iters per core, $PROTOCOL protocol"
echo "Topologies: ${TOPOLOGIES[*]}"
echo "Core counts: $(for c in "${CONFIGS[@]}"; do echo -n "${c%% *} "; done)"
echo "Results dir: $RESULTS_DIR"
echo ""

start_all=$(date +%s)

for topology in "${TOPOLOGIES[@]}"; do
    for cfg in "${CONFIGS[@]}"; do
        cores=$(echo "$cfg" | awk '{print $1}')
        rows=$(echo "$cfg" | awk '{print $2}')

        echo "=========================================================="
        echo "[$(date +%H:%M:%S)] topology=$topology cores=$cores rows=$rows"
        echo "=========================================================="

        out="$RESULTS_DIR/${topology}_${cores}cores.txt"
        start=$(date +%s)
        ./run_pingpong_strategies.sh $iters $cores $rows $topology $PROTOCOL \
            2>&1 | tee "$out"
        elapsed=$(($(date +%s) - start))
        echo "  ($elapsed s elapsed for this config)" | tee -a "$out"
        echo ""
    done
done

total=$(($(date +%s) - start_all))
echo "=========================================================="
echo "Sweep done. Total wall time: $total s ($((total/60)) min)"
echo "Per-config summaries in $RESULTS_DIR/"
echo "Per-core Summary.csv files in BMs/Synthetic/<N>Cores/PingPong-<strat>/${PROTOCOL}_<topology>_logs/"
