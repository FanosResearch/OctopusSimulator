#!/usr/bin/env bash
# live.sh -- watch one act of the demo AS IT SIMULATES: configure it exactly as
# demo/run_acts.sh does, start Octopus in the background, and serve the page with
# demo/live_server.py tailing the job report it writes (docs/Tasks.md).
#
#   bash demo/live.sh            # act2 (contention), the act worth watching
#   bash demo/live.sh act3       # protected: RR bus + one LLC way reserved for core 0
#   bash demo/live.sh act1       # the task alone
#
# Then open the printed URL and press "Live". The page replays each job the moment the
# simulator reports it and holds when it catches up. Ctrl-C stops both.
# Env: PORT (8770), JOBS_OVERRIDE (attr,jobs for a longer run), PY (python).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../sweeps" && pwd)/sweep_common.sh"
DEMO="$SWEEP_ROOT/demo"; PORT="${PORT:-8770}"; PY="${PY:-python}"
ACT="${1:-act2}"

case "$ACT" in
  act1) WL=alone;      ARB=FCFSArbiter; PART="";    LABEL="act 1 - task alone, FCFS bus";;
  act2) WL=contention; ARB=FCFSArbiter; PART="";    LABEL="act 2 - 3 streaming cores, FCFS bus";;
  act3) WL=contention; ARB=RRArbiter;   PART="0:0"; LABEL="act 3 - RR bus + LLC way reserved for core 0";;
  *) echo "usage: bash demo/live.sh [act1|act2|act3]" >&2; exit 1;;
esac
D="$DEMO/workloads/$WL"

# same shared config as run_acts.sh; restored when this script exits (Octopus reads it at startup)
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
SIM_PID=""
cleanup(){ [ -n "$SIM_PID" ] && kill "$SIM_PID" 2>/dev/null; cp "$_sbc_bak" "$SBC"; cp "$_cfg_bak" "$CFG"; rm -f "$_sbc_bak" "$_cfg_bak"; }
trap cleanup EXIT INT TERM
gen_baseline
set_arbiter "$ARB"
set_csv 'llc_controller\.arbiter_type\(s\),' "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'             "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'              "mcsim_scheduler(s),FRFCFS,,,,,"

mkdir -p "$D/newLogger"; rm -f "$D/newLogger"/*.csv
echo "== $LABEL"
echo "   workload $D"
"$BIN" -s MultiCoreSystem -p "workload_path(s)=$(cygpath -m "$D" 2>/dev/null || echo "$D")/" \
       -p "llc_controller.m_data_handler.way_partition(s)=$PART" > "$D/.out.live" 2>&1 &
SIM_PID=$!
echo "   Octopus running (pid $SIM_PID) - the page follows its JobReport as it is written"
# not exec: keep this shell alive so the trap stops the simulator and restores the config on Ctrl-C
"$PY" -u "$DEMO/live_server.py" --watch "$D" --port "$PORT" --label "$LABEL"
