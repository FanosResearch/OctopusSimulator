#!/bin/bash
#
# Run the ping-pong experiment for all three pairing strategies on a chosen
# topology, and summarize per-core WCL. Sweeps the hop-distance axis to give
# the reviewer's "antipodal vs neighbor vs random" comparison data.
#
# Usage:
#   ./run_pingpong_strategies.sh <iters> <cores> <rows> [topology] [protocol]
#
#   iters     iterations per core (e.g. 10000)
#   cores     number of cores (must equal rows * cols)
#   rows      mesh row count; cols is derived = cores / rows
#   topology  mesh2d | fullyconnected     (default: mesh2d)
#   protocol  MSI | MESI | MOESI          (default: MESI)
#
# Examples:
#   ./run_pingpong_strategies.sh 10000 16 4 mesh2d MESI       # 4x4 mesh
#   ./run_pingpong_strategies.sh 10000 32 8 mesh2d MESI       # 8x4 mesh
#   ./run_pingpong_strategies.sh 10000 16 4 fullyconnected    # K17 graph

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"
BMs_ROOT="${BMs_ROOT:-$PROJECT_ROOT/BMs}"
SIM_BIN="${SIM_BIN:-$PROJECT_ROOT/build/CMSpec_Simulator}"

iters=${1:?missing iters}
cores=${2:?missing cores}
rows=${3:?missing rows}
topology=${4:-mesh2d}
protocol=${5:-MESI}

# Per-run timeout in seconds. 10K iters at 16 cores on release build runs in
# a few minutes; allow generous headroom for 32-core runs.
TIMEOUT_S=${TIMEOUT_S:-1800}

# Validate inputs.
if (( cores % rows != 0 )); then
    echo "ERROR: rows ($rows) must divide cores ($cores)" >&2
    exit 1
fi
cols=$(( cores / rows ))

if [[ "$topology" != "mesh2d" && "$topology" != "fullyconnected" ]]; then
    echo "ERROR: unknown topology '$topology' (use mesh2d or fullyconnected)" >&2
    exit 1
fi

if [[ "$protocol" != "MSI" && "$protocol" != "MESI" && "$protocol" != "MOESI" ]]; then
    echo "ERROR: unknown protocol '$protocol' (use MSI, MESI, or MOESI)" >&2
    exit 1
fi

# ----------------------------------------------------------------------------
# build_sim_cmd <strategy>
#   Builds the -p flag string into the global `sim` variable based on the
#   topology selected at script invocation. Cores/grid are read from globals.
# ----------------------------------------------------------------------------
build_sim_cmd() {
    local workload_dir=$1
    local logs=$2

    local core_ids=""
    for ((i=0; i<cores; i++)); do core_ids+="$i,"; done
    local LLC_id=64

    sim="$SIM_BIN -s PredictableDirectorySystem"
    sim+=" -p 'workload_path(s)=$workload_dir/'"
    sim+=" -p 'logger_path(s)=$logs/'"
    sim+=" -p 'cache_controller[*].protocol_type(s)=DIRECTORY_$protocol'"
    sim+=" -p 'cache_controller[*].fsm_filename(s)=${protocol}_directory'"
    sim+=" -p 'llc_controller.protocol_type(s)=DIRECTORY_LLC_$protocol'"
    sim+=" -p 'llc_controller.fsm_filename(s)=${protocol}_LLC_directory'"
    sim+=" -p 'num_cores(i)=$cores'"
    sim+=" -p 'llc_controller.arbiter_candidates_ids(vi)=$core_ids'"
    sim+=" -p 'llc_controller.processing_q_arbiter_candidates_ids(vi)=$core_ids'"
    sim+=" -p 'cache_controller[*].processing_q_arbiter_candidates_ids(vi)=$core_ids'"
    sim+=" -p 'm_main_memory.processing_q_arbiter_candidates_ids(vi)=$core_ids'"
    sim+=" -p 'm_main_memory.m_llc_id(i)=$LLC_id'"
    sim+=" -p 'm_main_memory.m_memory_latency(i)=250'"
    sim+=" -p 'bus.m_lower_level_ids(vi)=$LLC_id'"
    sim+=" -p 'bus.interconnect_controller.candidates_id(vi)=$core_ids'"
    sim+=" -p 'cache_controller[*].processing_queue_size(i)=-1'"
    sim+=" -p 'cache_controller[*].m_shared_memory_id(i)=$LLC_id'"
    for ((id=0; id<cores; id++)); do
        sim+=" -p 'cache_controller[$id].m_id(i)=$id'"
    done
    sim+=" -p 'llc_controller.processing_queue_size(i)=-1'"
    sim+=" -p 'llc_controller.m_id(i)=$LLC_id'"

    # Buffer size scales with core count. The LLC has all cores hammering its
    # ingress queue, so undersizing causes "FIFO is Full" exits at high N.
    local buf=128
    (( cores >= 16 )) && buf=256
    (( cores >= 32 )) && buf=512

    if [[ "$topology" == "mesh2d" ]]; then
        # 2D-mesh NoC: one router per core, neighbors via N/S/E/W links.
        # LLC sits at corner router R(0,0).
        local switch_ids=""
        for ((s=0; s<cores; s++)); do switch_ids+="$((100+s)),"; done

        sim+=" -p 'interconnect_type(s)=NoC'"
        sim+=" -p 'interconnect.buffers_max_size(i)=$buf'"
        sim+=" -p 'interconnect.interconnect_controller.m_candidates_ids(vi)=$core_ids'"
        sim+=" -p 'interconnect.component_ids(vi),${core_ids}$LLC_id,'"
        sim+=" -p 'interconnect.switch_ids(vi),$switch_ids'"
        for ((id=0; id<cores; id++)); do
            sim+=" -p 'interconnect.connection_matrix[$id](vi),$id,$((100+id)),'"
        done
        sim+=" -p 'interconnect.connection_matrix[$cores](vi),$LLC_id,100,'"
        for ((k=0; k<cores; k++)); do
            i=$((k/cols)); j=$((k%cols))
            local nb="$k,"
            [ $k -eq 0 ] && nb+="$LLC_id,"
            [ $i -gt 0 ] && nb+="$((100+(i-1)*cols+j)),"
            [ $i -lt $((rows-1)) ] && nb+="$((100+(i+1)*cols+j)),"
            [ $j -lt $((cols-1)) ] && nb+="$((100+i*cols+(j+1))),"
            [ $j -gt 0 ] && nb+="$((100+i*cols+(j-1))),"
            sim+=" -p 'interconnect.connection_matrix[$((cores+1+k))](vi),$((100+k)),$nb'"
        done
    else
        # Fully-connected: every core connects to every other core AND the LLC.
        # LLC connects to all cores. Matches the original Mesh.csv layout.
        # interconnect_type=Mesh means single-hop point-to-point arbiters.
        sim+=" -p 'interconnect_type(s)=Mesh'"
        sim+=" -p 'interconnect.buffers_max_size(i)=$buf'"
        sim+=" -p 'interconnect.interconnect_controller.m_candidates_ids(vi)=$core_ids'"
        sim+=" -p 'interconnect.component_ids(vi),${core_ids}$LLC_id,'"
        # Each core: row "core_id, all_other_cores, LLC"
        for ((id=0; id<cores; id++)); do
            local nb=""
            for ((o=0; o<cores; o++)); do
                [ $o -ne $id ] && nb+="$o,"
            done
            nb+="$LLC_id,"
            sim+=" -p 'interconnect.connection_matrix[$id](vi),$id,$nb'"
        done
        # LLC row: connects to all cores.
        sim+=" -p 'interconnect.connection_matrix[$cores](vi),$LLC_id,$core_ids'"
    fi

    sim+=" -p 'cache_controller[*].m_data_handler.cache_size(i)=32768'"
    sim+=" -p 'cache_controller[*].m_data_handler.m_ways_count(i)=4'"
    sim+=" -p 'llc_controller.m_data_handler.cache_size(i)=4194304'"
    sim+=" -p 'llc_controller.m_data_handler.m_ways_count(i)=8'"

    # bus (LLC<->main memory) ingress buffer. Default in
    # PredictableDirectorySystem.csv is 32 -- fine for 4-16 cores but the
    # LLC overflows it at 32 cores. Same scaling rule as the interconnect.
    sim+=" -p 'bus.buffers_max_size(i)=$buf'"
    sim+=" -p 'interconnect.interconnect_controller.arbiter_type(s)=FCFSArbiter'"
    sim+=" -p 'bus.interconnect_controller.arbiter_type(s)=FCFSArbiter'"
    sim+=" -p 'llc_controller.arbiter_type(s)=FCFSArbiter'"
    sim+=" -p 'llc_controller.processing_q_arbiter_t(s)=NULL'"
    sim+=" -p 'cache_controller[*].processing_q_arbiter_t(s)=NULL'"
    sim+=" -p 'm_main_memory.processing_q_arbiter_t(s)=NULL'"
    sim+=" -p 'system_arbiter(s)=FCFSArbiter'"
    sim+=" -p 'RROF_size_limit(i)=24'"
}

run_one() {
    local strat=$1
    local tag=${topology}_${cores}c_${strat}
    local workload_dir="$BMs_ROOT/Synthetic/${cores}Cores/PingPong-$strat"
    local logs="$workload_dir/${protocol}_${topology}_logs"

    # Generate the trace per the chosen pairing. Only wipe the topology-
    # specific logs subdir, NOT the workload_dir itself -- workload_dir is
    # shared across topologies (different topologies coexist in
    # MESI_mesh2d_logs/, MESI_fullyconnected_logs/, etc.). Wiping the
    # workload_dir on every run nuked the prior topology's Summary.csv.
    rm -rf "$logs"
    "$PROJECT_ROOT/pingpong_synthetic" $cores $rows $iters "$workload_dir" \
        $strat 2 2>/dev/null > /dev/null
    mkdir -p "$logs"

    local sim
    build_sim_cmd "$workload_dir" "$logs"

    timeout $TIMEOUT_S bash -c "$sim" > "$logs/sim.out" 2>&1
    local rc=$?

    local summary="$logs/Summary.csv"
    if [ -f "$summary" ] && [ $(wc -l < "$summary") -gt 1 ]; then
        local wcl_min=$(tail -n +2 "$summary" | cut -d, -f2 | sort -n | head -1)
        local wcl_max=$(tail -n +2 "$summary" | cut -d, -f2 | sort -n | tail -1)
        local finish_max=$(tail -n +2 "$summary" | cut -d, -f14 | sort -n | tail -1)
        printf "%-10s rc=%s  WCL min/max = %s / %s  spread=%.2fx  finish_max=%s\n" \
            "$strat" "$rc" "$wcl_min" "$wcl_max" \
            $(awk "BEGIN{print $wcl_max/$wcl_min}") "$finish_max"
    else
        local err=$(grep -E "Error|Fault" "$logs/sim.out" 2>/dev/null | head -1)
        printf "%-10s FAILED (rc=%s): %s\n" "$strat" "$rc" "$err"
    fi
}

echo "${cores}-core ${rows}x${cols} ${topology}, $iters iters per core, ${protocol} protocol"
echo "=================================================================="
for s in antipodal neighbor random; do
    run_one $s
done
