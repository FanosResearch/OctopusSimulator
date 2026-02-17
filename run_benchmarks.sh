#!/bin/bash

# Create the results directory if it doesn't exist
mkdir -p results

# Define the path to the benchmarks
# Based on your example command, BMs are one level up from the executable
#BM_ROOT="../BMs/eembc-traces"
BM_ROOT="../BMs/splash"
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

# Benchmarks to exclude (space-separated)
EXCLUDE="barnes cholesky"

# Loop through each subdirectory in the benchmark root
for bm_path in "$BM_ROOT"/*; do
    if [ -d "$bm_path" ]; then
        # Extract the benchmark name (folder name)
        bm_name=$(basename "$bm_path")

        # Skip excluded benchmarks
        if echo "$EXCLUDE" | grep -qw "$bm_name"; then
            echo "Skipping excluded benchmark: $bm_name"
            continue
        fi

        echo "Running benchmark: $bm_name"

        # Create a directory for this benchmark run
        mkdir -p "../results/${bm_name}"

        # Run the simulator and redirect output to the log file
        ./CMSpec_Simulator --CfgFile=../tc_FR_4E.xml --BMsPath="$bm_path" --LogFileGenEnable=0 > "../results/${bm_name}/${bm_name}.log" 2>&1

        # Move generated CSV files to the result directory
        mv ../*.csv "../results/${bm_name}/" 2>/dev/null
    fi
done

echo "All benchmarks completed. Logs are stored in results/"
