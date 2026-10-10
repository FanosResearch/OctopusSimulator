#!/usr/bin/env bash
# Sweep the cache replacement policy: LRU vs RANDOM (applied to L1 and LLC).
# Set via the system CSV (newline-guarded append -- the key is not in the preset).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

VALUES="LRU RANDOM"
apply_value(){
  gen_baseline
  set_csv 'cache_controller\[\*\]\.m_data_handler\.replacement_policy_name\(s\),' \
          "cache_controller[*].m_data_handler.replacement_policy_name(s),$1,,,,,"
  set_csv 'llc_controller\.m_data_handler\.replacement_policy_name\(s\),' \
          "llc_controller.m_data_handler.replacement_policy_name(s),$1,,,,,"
}
run_axis replacement policy
