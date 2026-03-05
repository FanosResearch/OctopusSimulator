#!/bin/bash
# Run all SPLASH benchmarks in parallel (RROF + FRFCFS) and record timing
# Adapted from grrof branch for gem5_ARM_Challenge

# Resolve absolute paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"
SIM="$BUILD_DIR/CMSpec_Simulator"
BM_ROOT="$PROJECT_ROOT/BMs/splash"

# Config files (4 cores, 1 LLC bank, MCsim enabled, logFileGenEnable=1)
RROF_CFG="$PROJECT_ROOT/test/arm_challenge/tc_FR_4C_1B_rrof.xml"
FRFCFS_CFG="$PROJECT_ROOT/test/arm_challenge/tc_FR_4C_1B_frfcfs.xml"

# LD_LIBRARY_PATH for MCsim
export LD_LIBRARY_PATH="$PROJECT_ROOT/MCsim/src:${LD_LIBRARY_PATH:-}"

# Benchmarks to exclude (space-separated, empty = run all)
EXCLUDE=""

# Create results directory
RESULTS_DIR="$PROJECT_ROOT/results"
mkdir -p "$RESULTS_DIR"

# Check prerequisites
if [ ! -x "$SIM" ]; then
    echo "Error: $SIM not found or not executable. Run 'make' in build/ first."
    exit 1
fi
if [ ! -d "$BM_ROOT" ]; then
    echo "Error: Benchmark directory $BM_ROOT not found."
    exit 1
fi
if [ ! -f "$RROF_CFG" ]; then
    echo "Error: RROF config $RROF_CFG not found."
    exit 1
fi
if [ ! -f "$FRFCFS_CFG" ]; then
    echo "Error: FRFCFS config $FRFCFS_CFG not found."
    exit 1
fi

# Timing summary file
TIMING_SUMMARY="$RESULTS_DIR/timing_summary.csv"
echo "benchmark,scheduler,wall_time_sec,max_rss_kb,exit_code,finish_cycle_c0,avg_latency_c0" > "$TIMING_SUMMARY"

run_benchmark() {
    local bm_path="$1"
    local bm_name="$2"
    local sched="$3"       # RROF or FRFCFS
    local base_cfg="$4"
    local results_dir="$RESULTS_DIR/${bm_name}_${sched}"

    mkdir -p "$results_dir"

    # Create a per-benchmark config with its own loggerPath
    local bm_cfg="$results_dir/tc_${bm_name}_${sched}.xml"
    sed "s|loggerPath *= *\"[^\"]*\"|loggerPath=\"${results_dir}/\"|" "$base_cfg" > "$bm_cfg"

    echo "[$(date +%H:%M:%S)] Started: $bm_name ($sched)"

    # Run from build/ so getcwd()-based FSM path resolves correctly
    cd "$BUILD_DIR"
    /usr/bin/time -v "$SIM" --CfgFile="$bm_cfg" --BMsPath="$bm_path" \
        > "$results_dir/${bm_name}.log" 2>"$results_dir/time.txt"
    local exit_code=$?

    # Extract wall time and RSS from /usr/bin/time output
    local wall_time=$(grep "Elapsed (wall clock)" "$results_dir/time.txt" | sed 's/.*: //')
    local max_rss=$(grep "Maximum resident" "$results_dir/time.txt" | sed 's/.*: //')

    # Convert wall time (h:mm:ss or m:ss) to seconds
    local wall_sec=0
    local ncolons=$(echo "$wall_time" | tr -cd ':' | wc -c)
    if [ "$ncolons" -ge 2 ]; then
        wall_sec=$(echo "$wall_time" | awk -F: '{printf "%.0f", $1*3600+$2*60+$3}')
    elif [ "$ncolons" -eq 1 ]; then
        wall_sec=$(echo "$wall_time" | awk -F: '{printf "%.0f", $1*60+$2}')
    fi

    # Extract finish cycle and avg latency for core 0 from Summary.csv
    local finish_cycle=""
    local avg_lat=""
    if [ -f "$results_dir/Summary.csv" ]; then
        local core0_line=$(grep "^0," "$results_dir/Summary.csv")
        if [ -n "$core0_line" ]; then
            finish_cycle=$(echo "$core0_line" | awk -F, '{print $NF}')
            avg_lat=$(echo "$core0_line" | awk -F, '{print $(NF-1)}')
        fi
    fi

    # Clean up the per-benchmark config
    rm -f "$bm_cfg"

    if [ $exit_code -eq 0 ]; then
        echo "[$(date +%H:%M:%S)] Completed: $bm_name ($sched) - wall=$wall_time RSS=${max_rss}KB"
    else
        echo "[$(date +%H:%M:%S)] FAILED: $bm_name ($sched) exit=$exit_code - wall=$wall_time"
    fi

    # Append to timing summary (thread-safe with flock)
    (
        flock -x 200
        echo "$bm_name,$sched,$wall_sec,$max_rss,$exit_code,$finish_cycle,$avg_lat" >> "$TIMING_SUMMARY"
    ) 200>"$TIMING_SUMMARY.lock"
}

# Launch all benchmarks in parallel (both RROF and FRFCFS for each)
pids=()
for bm_path in "$BM_ROOT"/*; do
    if [ -d "$bm_path" ]; then
        bm_name=$(basename "$bm_path")

        # Skip excluded benchmarks
        if [ -n "$EXCLUDE" ] && echo "$EXCLUDE" | grep -qw "$bm_name"; then
            echo "Skipping excluded benchmark: $bm_name"
            continue
        fi

        run_benchmark "$bm_path" "$bm_name" "RROF" "$RROF_CFG" &
        pids+=($!)
        run_benchmark "$bm_path" "$bm_name" "FRFCFS" "$FRFCFS_CFG" &
        pids+=($!)
    fi
done

echo ""
echo "Launched ${#pids[@]} benchmark runs in parallel. Waiting for completion..."
echo ""

# Wait for all background jobs
failed=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        ((failed++))
    fi
done

# Clean up lock file
rm -f "$TIMING_SUMMARY.lock"

echo ""
echo "=== ALL BENCHMARKS COMPLETE ==="
echo "$failed failed out of ${#pids[@]} runs."
echo ""
echo "=== TIMING SUMMARY ==="
column -t -s',' "$TIMING_SUMMARY" 2>/dev/null || cat "$TIMING_SUMMARY"
echo ""
echo "Results in: $RESULTS_DIR/"
echo "Timing summary: $TIMING_SUMMARY"
