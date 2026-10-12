#!/usr/bin/env bash
# Sweep the main-memory model: MainMemoryController (fixed latency) vs MCsim
# (cycle-accurate DDR4, FR-FCFS scheduler). Set via the system CSV top-level
# key main_memory_type (newline-guarded append). Relevant metric: wcDRAM / avg.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

VALUES="MainMemoryController MCsim"
apply_value(){
  gen_baseline
  [ "$1" = "MCsim" ] && set_csv 'main_memory_type\(s\),' "main_memory_type(s),MCsim,,,,,"
  return 0
}
run_axis memory memory_model
