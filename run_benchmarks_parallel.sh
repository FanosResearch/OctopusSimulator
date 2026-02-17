#!/bin/bash

# Create the results directory if it doesn't exist
mkdir -p results

# Define the path to the benchmarks
#BM_ROOT="../BMs/eembc-traces"
BM_ROOT="../BMs/splash"

# Benchmarks to exclude (space-separated)
#EXCLUDE="barnes cholesky"
EXCLUDE=""
# Resolve absolute paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Check if simulator executable exists
if [ ! -x "./CMSpec_Simulator" ]; then
    echo "Error: ./CMSpec_Simulator not found or not executable in the current directory."
    exit 1
fi

# Check if benchmark directory exists
if [ ! -d "$BM_ROOT" ]; then
    echo "Error: Benchmark directory $BM_ROOT not found."
    exit 1
fi

# Base config file
BASE_CFG="../tc_FR_4E.xml"

run_benchmark() {
    local bm_path="$1"
    local bm_name="$2"
    local results_dir="$PROJECT_ROOT/results/${bm_name}"

    mkdir -p "$results_dir"

    # Create a per-benchmark config with its own loggerPath
    # so each run writes CSVs to its own results directory
    local bm_cfg="$results_dir/tc_FR_4E_${bm_name}.xml"
    sed "s|loggerPath *= *\"[^\"]*\"|loggerPath=\"${results_dir}/\"|" "$BASE_CFG" > "$bm_cfg"

    echo "[$(date +%H:%M:%S)] Started benchmark: $bm_name"

    ./CMSpec_Simulator --CfgFile="$bm_cfg" --BMsPath="$bm_path" --LogFileGenEnable=0 \
        > "$results_dir/${bm_name}.log" 2>&1
    local exit_code=$?

    # Clean up the per-benchmark config
    rm -f "$bm_cfg"

    if [ $exit_code -eq 0 ]; then
        echo "[$(date +%H:%M:%S)] Completed benchmark: $bm_name"
    else
        echo "[$(date +%H:%M:%S)] FAILED benchmark: $bm_name (exit code $exit_code)"
    fi
}

# Launch all benchmarks in parallel
pids=()
for bm_path in "$BM_ROOT"/*; do
    if [ -d "$bm_path" ]; then
        bm_name=$(basename "$bm_path")

        # Skip excluded benchmarks
        if echo "$EXCLUDE" | grep -qw "$bm_name"; then
            echo "Skipping excluded benchmark: $bm_name"
            continue
        fi

        run_benchmark "$bm_path" "$bm_name" &
        pids+=($!)
    fi
done

echo "Launched ${#pids[@]} benchmarks in parallel. Waiting for completion..."

# Wait for all background jobs
failed=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        ((failed++))
    fi
done

echo "All benchmarks finished. $failed failed. Results in: $PROJECT_ROOT/results/"
