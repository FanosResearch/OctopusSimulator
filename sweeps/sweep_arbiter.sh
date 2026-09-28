#!/usr/bin/env bash
# Sweep the interconnect (bus) arbiter: FCFS vs RR vs TDM.
# The arbiter lives in the controller's Extends file (SplitBusController.csv),
# not the system CSV, so it is set with set_arbiter and restored at the end.
# Relevant metric: wcReqBus / wcRespBus (and wcTotal). TDM trades higher WCET
# for time-triggered predictability.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sweep_common.sh"

VALUES="FCFSArbiter RRArbiter TDMArbiter"
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"
trap 'cp "$_sbc_bak" "$SBC"; rm -f "$_sbc_bak"' EXIT   # always restore the arbiter file

apply_value(){ gen_baseline; set_arbiter "$1"; }
run_axis arbiter arbiter
