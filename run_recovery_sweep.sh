#!/bin/bash
#
# Re-run the configs that got wiped by the bug in the first sweep:
#   - mesh2d for N=2/4/8/16/32
#   - fullyconnected for N=32 only (others survived)
#
# Uses the patched run_pingpong_strategies.sh which (a) keeps the
# workload_dir between topology runs and (b) scales buffer size with N.

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"
ITERS=${1:-10000}

echo "=== mesh2d 2 cores ==="
bash run_pingpong_strategies.sh $ITERS 2  2 mesh2d MESI

echo "=== mesh2d 4 cores ==="
bash run_pingpong_strategies.sh $ITERS 4  2 mesh2d MESI

echo "=== mesh2d 8 cores ==="
bash run_pingpong_strategies.sh $ITERS 8  4 mesh2d MESI

echo "=== mesh2d 16 cores ==="
bash run_pingpong_strategies.sh $ITERS 16 4 mesh2d MESI

echo "=== mesh2d 32 cores ==="
bash run_pingpong_strategies.sh $ITERS 32 8 mesh2d MESI

echo "=== fullyconnected 32 cores ==="
bash run_pingpong_strategies.sh $ITERS 32 8 fullyconnected MESI

echo "DONE"
