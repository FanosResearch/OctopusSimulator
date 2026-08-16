#!/bin/bash
#
# 16-core 4x4 2D-mesh NoC sweep for SPLASH-3.
# Each core C_k attaches to its local router R_(100+k). Routers form a 4x4
# grid wired by N/S/E/W links. The LLC (id 64) is placed at the R100 corner.
# Hop count grows from 1 (local) up to 6 (corner-to-corner: C15 -> R115 ->
# R111 -> R107 -> R103 -> R102 -> R101 -> R100 -> LLC ~ 7 link traversals,
# capturing the worst case the reviewer asked about for 2D meshes.
#
# Usage:  ./test_splash_NoC_2dmesh_16core.sh <PROTOCOL>
#   e.g.  ./test_splash_NoC_2dmesh_16core.sh MSI

# Resolve paths relative to this script's location. Override either by
# exporting PROJECT_ROOT / BMs_ROOT / SIM_BIN before invoking the script.
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"
BMs_ROOT="${BMs_ROOT:-$PROJECT_ROOT/BMs}"
SIM_BIN="${SIM_BIN:-$PROJECT_ROOT/build/CMSpec_Simulator}"

cores=16
rows=4
cols=4
# BMs_path="/Users/Mhossam/Documents/PhD_Work/CacheSim_cmake/cachesim/BMs/Splash/${cores}Cores/"
BMs_path="${BMs_ROOT}/Splash/${cores}Cores/"

simulate()
{
    logs="${BMs_path}${1}/${2}_logs/"
    mkdir -p $logs

    # ---- IDs ----
    core_ids=""
    for ((ids=0; ids<$cores; ids++))
    do
        core_ids+="${ids},"
    done
    LLC_id=64

    # Router IDs: R(i,j) = 100 + i*cols + j   (i=row, j=col)
    switch_ids=""
    for ((s=0; s<$cores; s++))
    do
        switch_ids+="$((100+s)),"
    done

    # sim="/Users/Mhossam/Documents/PhD_Work/CacheSim_cmake/cachesim/build/CMSpec_Simulator"
    sim="${SIM_BIN}"
    sim+=" -s PredictableDirectorySystem"
    sim+=" -p 'workload_path(s)=${BMs_path}${1}/'"
    sim+=" -p 'logger_path(s)=${logs}'"
    sim+=" -p 'cache_controller[*].protocol_type(s)=DIRECTORY_${2}'"
    sim+=" -p 'cache_controller[*].fsm_filename(s)=${2}_directory'"
    sim+=" -p 'llc_controller.protocol_type(s)=DIRECTORY_LLC_${2}'"
    sim+=" -p 'llc_controller.fsm_filename(s)=${2}_LLC_directory'"
    sim+=" -p 'num_cores(i)=${cores}'"
    sim+=" -p 'llc_controller.arbiter_candidates_ids(vi)=${core_ids}'"
    sim+=" -p 'llc_controller.processing_q_arbiter_candidates_ids(vi)=${core_ids}'"
    sim+=" -p 'cache_controller[*].processing_q_arbiter_candidates_ids(vi)=${core_ids}'"

    sim+=" -p 'm_main_memory.processing_q_arbiter_candidates_ids(vi)=${core_ids}'"
    sim+=" -p 'm_main_memory.m_llc_id(i)=$LLC_id'",
    sim+=" -p 'm_main_memory.m_memory_latency(i)=250'",

    sim+=" -p 'bus.m_lower_level_ids(vi)=$LLC_id'",
    sim+=" -p 'bus.interconnect_controller.candidates_id(vi)=${core_ids}'"

    sim+=" -p 'cache_controller[*].processing_queue_size(i)=-1'"
    sim+=" -p 'cache_controller[*].m_shared_memory_id(i)=$LLC_id'"
    for ((id=0; id<$cores; id++))
    do
        sim+=" -p 'cache_controller[$id].m_id(i)=$id'"
    done
    sim+=" -p 'llc_controller.processing_queue_size(i)=-1'"
    sim+=" -p 'llc_controller.m_id(i)=$LLC_id'"

    # ---- NoC topology: 4x4 2D mesh ----
    sim+=" -p 'interconnect_type(s)=NoC'"
    # Buffer per the audit (issue #4). NOTE: single-switch NoC script uses 256;
    # 128 may be tight under heavy 16-core contention. Bump if you hit the
    # "full buffer" exit from MeshController.
    sim+=" -p 'interconnect.buffers_max_size(i)=128'"
    sim+=" -p 'interconnect.interconnect_controller.m_candidates_ids(vi)=${core_ids}'"
    sim+=" -p 'interconnect.component_ids(vi),${core_ids}$LLC_id,'"
    sim+=" -p 'interconnect.switch_ids(vi),${switch_ids}'"

    # connection_matrix[0..cores-1]: each core attaches only to its local router.
    for ((id=0; id<$cores; id++))
    do
        local_router=$((100+id))
        sim+=" -p 'interconnect.connection_matrix[$id](vi),$id,$local_router,'"
    done

    # connection_matrix[cores]: LLC attaches only to corner router R100.
    sim+=" -p 'interconnect.connection_matrix[$cores](vi),$LLC_id,100,'"

    # connection_matrix[cores+1 .. cores+1+cores-1]: routers wired in a grid.
    # R(i,j) at index k = i*cols + j gets: local core (k), LLC if k==0, plus
    # N/S/E/W router neighbors that exist within the grid bounds.
    for ((k=0; k<$cores; k++))
    do
        i=$((k / cols))
        j=$((k % cols))
        router_id=$((100+k))
        neighbors="$k,"                   # local core
        if [ $k -eq 0 ]; then
            neighbors+="$LLC_id,"          # LLC sits at R100 corner only
        fi
        # North
        if [ $i -gt 0 ]; then
            neighbors+="$((100+(i-1)*cols+j)),"
        fi
        # South
        if [ $i -lt $((rows-1)) ]; then
            neighbors+="$((100+(i+1)*cols+j)),"
        fi
        # East
        if [ $j -lt $((cols-1)) ]; then
            neighbors+="$((100+i*cols+(j+1))),"
        fi
        # West
        if [ $j -gt 0 ]; then
            neighbors+="$((100+i*cols+(j-1))),"
        fi
        idx=$((cores+1+k))
        sim+=" -p 'interconnect.connection_matrix[$idx](vi),$router_id,$neighbors'"
    done

    sim+=" -p 'cache_controller[*].m_data_handler.cache_size(i)=32768'"
    sim+=" -p 'cache_controller[*].m_data_handler.m_ways_count(i)=4'"
    sim+=" -p 'llc_controller.m_data_handler.cache_size(i)=4194304'"
    sim+=" -p 'llc_controller.m_data_handler.m_ways_count(i)=8'"

    sim+=" -p 'interconnect.interconnect_controller.arbiter_type(s)=FCFSArbiter'",
    sim+=" -p 'bus.interconnect_controller.arbiter_type(s)=FCFSArbiter'",
    sim+=" -p 'llc_controller.arbiter_type(s)=FCFSArbiter'",
    sim+=" -p 'llc_controller.processing_q_arbiter_t(s)=NULL'",
    sim+=" -p 'cache_controller[*].processing_q_arbiter_t(s)=NULL'",
    sim+=" -p 'm_main_memory.processing_q_arbiter_t(s)=NULL'",
    sim+=" -p 'system_arbiter(s)=FCFSArbiter'",
    sim+=" -p 'RROF_size_limit(i)=24'",
    # sim+=" -p 'system_arbiter(s)=RROFArbiter'",

    date
    echo $sim
    eval $sim

    echo "******************${1}******************" >> "${BMs_path}${2}_output.txt"
    cat "${logs}/Summary.csv" >> "${BMs_path}${2}_output.txt"
}

run()
{
    protocol=$1
    touch "${BMs_path}${protocol}_output.txt"

    echo "************************************${protocol} Output************************************" > "${BMs_path}${protocol}_output.txt"

    simulate BARNS $protocol
    simulate FFT $protocol
    simulate FMM $protocol
    simulate LU $protocol
    simulate RADIOSITY $protocol
    simulate RADIX $protocol
#     simulate WATER $protocol
}

# run MSI
# run MESI
# run MOESI
run $1
