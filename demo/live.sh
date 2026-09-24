#!/usr/bin/env bash
# live.sh -- watch the demo AS IT SIMULATES: configure the act(s) exactly as demo/run_acts.sh
# does, start Octopus in the background, and serve the page with demo/live_server.py tailing
# the job report(s) it writes (docs/Tasks.md).
#
#   bash demo/live.sh compare    # all three acts at once -> three robots on one map
#   bash demo/live.sh act2       # one act: contention, the one worth watching
#   bash demo/live.sh act3       # protected: RR bus + one LLC way reserved for core 0
#   bash demo/live.sh act1       # the task alone
#
# Then open the printed URL and press "Live" (or "Compare"). The page replays each job the
# moment the simulator reports it and holds when it catches up. Ctrl-C stops everything.
#
# The acts differ only by -p overrides (bus arbiter, LLC way partition) and their workload
# directory, so running them together is safe: no shared file is edited while they run.
# Env: PORT (8770), PY (python).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../sweeps" && pwd)/sweep_common.sh"
DEMO="$SWEEP_ROOT/demo"; PORT="${PORT:-8770}"; PY="${PY:-python}"
WHAT="${1:-compare}"

case "$WHAT" in
  compare) ACTS="act1 act2 act3";;
  act1|act2|act3) ACTS="$WHAT";;
  *) echo "usage: bash demo/live.sh [compare|act1|act2|act3]" >&2; exit 1;;
esac

act_flags(){ case "$1" in act3) echo 'RRArbiter|0:0';; *) echo 'FCFSArbiter|';; esac; }
act_label(){
  case "$1" in
    act1) echo "act 1 - task alone, FCFS bus";;
    act2) echo "act 2 - 3 streaming cores, FCFS bus";;
    act3) echo "act 3 - RR bus + LLC way reserved for core 0";;
  esac
}

# shared config (same baseline as run_acts.sh); restored on exit -- Octopus reads it at startup
_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
PIDS=()
_cleaned=0
cleanup(){
  [ "$_cleaned" = "1" ] && return; _cleaned=1        # INT then EXIT both fire: only clean once
  for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null; done
  [ -f "$_sbc_bak" ] && cp "$_sbc_bak" "$SBC"; [ -f "$_cfg_bak" ] && cp "$_cfg_bak" "$CFG"
  rm -f "$_sbc_bak" "$_cfg_bak"
}
trap cleanup EXIT INT TERM
gen_baseline
set_csv 'llc_controller\.arbiter_type\(s\),' "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'             "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'              "mcsim_scheduler(s),FRFCFS,,,,,"

WATCH_ARGS=()
for a in $ACTS; do
  d="$DEMO/workloads/$a"; f="$(act_flags "$a")"; arb="${f%%|*}"; part="${f##*|}"
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv
  "$BIN" -s MultiCoreSystem -p "workload_path(s)=$(cygpath -m "$d" 2>/dev/null || echo "$d")/" \
         -p "bus[0].interconnect_controller.arbiter_type(s)=$arb" \
         -p "llc_controller.m_data_handler.way_partition(s)=$part" > "$d/.out.live" 2>&1 &
  PIDS+=($!)
  echo "== $(act_label "$a")  (pid ${PIDS[${#PIDS[@]}-1]}, $arb${part:+, way $part})"
  WATCH_ARGS+=(--watch "$a=$d")
done
echo "   the page follows each JobReport as it is written"
# not exec: keep this shell alive so the trap stops the simulators and restores the config
"$PY" -u "$DEMO/live_server.py" "${WATCH_ARGS[@]}" --port "$PORT"
