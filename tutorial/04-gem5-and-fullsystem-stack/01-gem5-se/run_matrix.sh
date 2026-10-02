#!/bin/bash
# run_matrix.sh -- the seven configurations of the interference demo under gem5 SE.
#
#   bash run_matrix.sh [config ...]      (default: all seven, in parallel)
#   LOG=1 bash run_matrix.sh ...         also Octopus's own per-request reports
#                                        (newLogger/; about 1 GB per run, twice
#                                        the run time); RUNS=<dir> to write elsewhere
#
# Each run writes runs/<config>/ (simout.txt carries the demo's report and its
# per-scan dump). About 5 minutes per run for Solo and the light aggressor,
# about 10 for the heavy one, all in parallel on a machine with 7+ cores.
#
#   A_solo        the SLAM pipeline alone
#   L_fcfs        + light aggressor: 2 MiB write sweep, fits the LLC (queueing)
#   L_rr          + light aggressor, round-robin bus and LLC arbitration
#   H_fcfs        + heavy aggressor: 16 MiB read sweep, twice the LLC (capacity)
#   H_rr          + heavy aggressor, round-robin
#   H_part        + heavy aggressor, LLC way partitioning
#   H_rr_part     + heavy aggressor, round-robin and way partitioning
#
# Settings: 4 cores (player, mapper, front-end, aggressor), scan period 80 us,
# a keyframe every 2nd scan, 40 scans, MultiCoreSystem_gem5 preset (FCFS).
# The aggressor thread is created first, so gem5 SE places it on core 1, whose
# L1s are Octopus ids 1 (instruction) and 5 (data): "1:0;5:0" confines its
# LLC fills to way 0.

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
GEM5_BIN=${GEM5_BIN:-$GEM5_ROOT/build/ARM/gem5.opt}
CONFIG=$ROOT/gem5/configs/se_arm.py
DEMO=$HERE/slam_demo/slam_demo
[ -x "$GEM5_BIN" ] || { echo "gem5 not found: set GEM5_ROOT or GEM5_BIN (built with EXTRAS=$ROOT)"; exit 1; }
[ -x "$DEMO" ] || { echo "build the demo first: make -C $HERE/slam_demo"; exit 1; }

RR=(--octopus-param 'bus[0].interconnect_controller.arbiter_type(s)=RRArbiter'
    --octopus-param 'llc_controller.arbiter_type(s)=RRArbiter')
PART=(--octopus-param 'llc_controller.m_data_handler.way_partition(s)=1:0;5:0')
LIGHT=(--aggr 1 --aggr-write --aggr-kib 2048)
HEAVY=(--aggr 1 --aggr-kib 16384)
DEMO_ARGS=(--period-us 80 --dump)
RUNS=${RUNS:-$HERE/runs}
LOGGING=()
[ "${LOG:-0}" = 1 ] && LOGGING=(--octopus-param 'cpu[*].log_requests(i)=1')

run() {   # run <name> <octopus options...> -- <demo options...>
    local name=$1; shift
    local oct=() 
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do oct+=("$1"); shift; done
    shift
    rm -rf "$RUNS/$name"
    "$GEM5_BIN" -re -d "$RUNS/$name" "$CONFIG" --num-cores 4 "${oct[@]}" "${LOGGING[@]}" \
        --binary "$DEMO" -- "${DEMO_ARGS[@]}" "$@" > /dev/null 2>&1
    echo "$name: $(grep -h '^\[ATE\]' "$RUNS/$name/simout.txt" 2>/dev/null || echo "no result, see $RUNS/$name/simerr.txt")"
}

ALL=(A_solo L_fcfs L_rr H_fcfs H_rr H_part H_rr_part)

one() {   # the options of one configuration; arrays, so no quoting is lost
    case $1 in
        A_solo)    run A_solo -- ;;
        L_fcfs)    run L_fcfs -- "${LIGHT[@]}" ;;
        L_rr)      run L_rr "${RR[@]}" -- "${LIGHT[@]}" ;;
        H_fcfs)    run H_fcfs -- "${HEAVY[@]}" ;;
        H_rr)      run H_rr "${RR[@]}" -- "${HEAVY[@]}" ;;
        H_part)    run H_part "${PART[@]}" -- "${HEAVY[@]}" ;;
        H_rr_part) run H_rr_part "${RR[@]}" "${PART[@]}" -- "${HEAVY[@]}" ;;
        *) echo "unknown config $1 (one of ${ALL[*]})"; return 1 ;;
    esac
}

mkdir -p "$RUNS"
for c in "${@:-${ALL[@]}}"; do
    one "$c" &
done
wait
echo "plots: python $HERE/slam_demo/viz/plot_matrix.py   (after make -C $HERE/slam_demo steps)"
