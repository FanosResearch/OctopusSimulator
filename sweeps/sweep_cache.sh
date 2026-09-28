#!/usr/bin/env bash
# Sweep the shared LLC capacity (bytes): 16K / 32K / 64K.
# Set via the system CSV (the key is present in the preset -> sed override).
# Relevant metric: wcTotal / avg / finish (bigger LLC -> fewer misses).
# (Extend VALUES / add m_ways_count or m_data_access_latency lines for more.)
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

VALUES="16384 32768 65536"
apply_value(){
  gen_baseline
  set_csv 'llc_controller\.m_data_handler\.cache_size\(i\),' \
          "llc_controller.m_data_handler.cache_size(i),$1,,,,,"
}
run_axis cache llc_size_bytes
