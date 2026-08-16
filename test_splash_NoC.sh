#!/bin/bash

# Resolve paths relative to this script's location. Override either by
# exporting PROJECT_ROOT / BMs_ROOT / SIM_BIN before invoking the script.
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"
BMs_ROOT="${BMs_ROOT:-$PROJECT_ROOT/BMs}"
SIM_BIN="${SIM_BIN:-$PROJECT_ROOT/build/CMSpec_Simulator}"

cores=$2
# BMs_path="/Users/Mhossam/Documents/PhD_Work/CacheSim_cmake/cachesim/BMs/Splash/${cores}Cores/"
BMs_path="${BMs_ROOT}/Splash/${cores}Cores/"

connection_mat()
{
    mat="$1,"
    for ((i=0; i<$cores; i++))
    do
        if (($i != $1)); then
            mat+="$i,"
        fi
    done
    echo $mat
}

simulate()
{
    logs="${BMs_path}${1}/${2}_logs/"
    mkdir -p $logs

    core_ids=""
    for ((ids=0; ids<$cores; ids++))
    do
        core_ids+="${ids},"
    done
    LLC_id=64
    switch_id=200

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
    # sim+=" -p 'bus.interconnect_controller.candidates_id(vi)=${core_ids}$LLC_id'"
    sim+=" -p 'bus.interconnect_controller.candidates_id(vi)=${core_ids}'"
    
    sim+=" -p 'cache_controller[*].processing_queue_size(i)=-1'"
    sim+=" -p 'cache_controller[*].m_shared_memory_id(i)=$LLC_id'"
    for ((id=0; id<$cores; id++))
    do
        sim+=" -p 'cache_controller[$id].m_id(i)=$id'"
    done
    sim+=" -p 'llc_controller.processing_queue_size(i)=-1'"
    sim+=" -p 'llc_controller.m_id(i)=$LLC_id'"

    sim+=" -p 'interconnect_type(s)=NoC'"
    sim+=" -p 'interconnect.buffers_max_size(i)=256'"
    sim+=" -p 'interconnect.interconnect_controller.m_candidates_ids(vi)=${core_ids}'"
    sim+=" -p 'interconnect.component_ids(vi),${core_ids}$LLC_id,'"
    
    sim+=" -p 'interconnect.switch_ids(vi),$switch_id'"
    for ((id=0; id<$cores; id++))
    do
        sim+=" -p 'interconnect.connection_matrix[$id](vi),$id,$switch_id,$LLC_id,'"
    done
    sim+=" -p 'interconnect.connection_matrix[$cores](vi),$LLC_id,${core_ids}'"
    sim+=" -p 'interconnect.connection_matrix[$((cores+1))](vi),$switch_id,${core_ids}'"

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