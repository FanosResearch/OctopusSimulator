#!/usr/bin/env bash
# run_acts.sh -- the three acts of the real-time tracking demo, each a cycle-accurate Octopus
# run of the periodic localization task (docs/Tasks.md) on core 0:
#
#   act1  alone       core 0 only (cores 1-3 have no workload), FCFS bus
#   act2  contention  core 0 + three streaming aggressors, FCFS bus
#   act3  protected   same load, Round-Robin bus + one LLC way reserved for core 0
#                     (llc_controller.m_data_handler.way_partition = "0:0")
#
# The acts differ ONLY by per-run -p overrides (bus arbiter, way partition) and by which
# workload directory they use, so they can run at the same time -- `demo/live.sh compare`
# does exactly that. The shared part of the config (snoop MESI + Exclusive L1, MCsim DDR4
# with FRFCFS, the paper's PCC-like setup) is written once here and restored on exit.
#
# Each act writes demo/workloads/<act>/newLogger/{JobReport_C0.csv, LatencyReport_C*.csv,
# Summary.csv}; extract_jobs.py turns them into demo/data/act<n>.jsonl (+ .manifest.json),
# plot_trajectory.py renders demo/figures/trajectory.pdf, build_page.py embeds them in the page.
#
# Usage: bash demo/run_acts.sh [act1|act2|act3 ...]     (default: all three, in parallel)
# Env:   CYCLE_NS (display scale for the page readouts, default 1.0), PY (python),
#        SEQ=1 (run the acts one after another instead of together)
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../sweeps" && pwd)/sweep_common.sh"
DEMO="$SWEEP_ROOT/demo"; DATA="$DEMO/data"; mkdir -p "$DATA"
CYCLE_NS="${CYCLE_NS:-1.0}"; PY="${PY:-python}"

_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
trap 'cp "$_sbc_bak" "$SBC"; cp "$_cfg_bak" "$CFG"; rm -f "$_sbc_bak" "$_cfg_bak"' EXIT

# --- the shared config, written once (the same baseline the sweeps use) ---------------
gen_baseline
set_csv 'llc_controller\.arbiter_type\(s\),'  "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'              "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'               "mcsim_scheduler(s),FRFCFS,,,,,"

# per-act knobs: bus arbiter and LLC way reservation, both as -p overrides
act_flags(){ case "$1" in act3) echo 'RRArbiter|0:0';; *) echo 'FCFSArbiter|';; esac; }
act_label(){
  case "$1" in
    act1) echo "alone: core 0 only, FCFS bus";;
    act2) echo "contention: 3 streaming cores, FCFS bus";;
    act3) echo "protected: RR bus + LLC way reserved for core 0";;
  esac
}

sim_one(){
  local act="$1"
  local d="$DEMO/workloads/$act"
  local f arb part rc
  f="$(act_flags "$act")"; arb="${f%%|*}"; part="${f##*|}"
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv
  "$BIN" -s MultiCoreSystem -p "workload_path(s)=$(cygpath -m "$d" 2>/dev/null || echo "$d")/" \
         -p "bus[0].interconnect_controller.arbiter_type(s)=$arb" \
         -p "llc_controller.m_data_handler.way_partition(s)=$part" > "$d/.out.$act" 2>&1
  rc=$?
  if [ $rc -ne 0 ] || grep -iq "cannot\|fault" "$d/.out.$act"; then
    echo "   $act FAILED (rc=$rc)"; tail -3 "$d/.out.$act"; return 1
  fi
}

acts="${*:-act1 act2 act3}"
for a in $acts; do echo "== $a: $(act_label "$a")  [$(act_flags "$a" | tr '|' ' ')]"; done
start=$(date +%s)
if [ "${SEQ:-0}" = "1" ]; then
  for a in $acts; do sim_one "$a"; done
else
  for a in $acts; do sim_one "$a" & done; wait
fi
echo "   simulated in $(( $(date +%s) - start )) s"

for a in $acts; do
  "$PY" "$DEMO/extract_jobs.py" "$DEMO/workloads/$a/newLogger" --cycle-ns "$CYCLE_NS" --label "$(act_label "$a")" -o "$DATA/$a.jsonl"
done
"$PY" "$DEMO/plot_trajectory.py" --data "$DATA" --out "$DEMO/figures" && "$PY" "$DEMO/build_page.py" --data "$DATA"
echo "-> $DATA, $DEMO/figures/trajectory.pdf, page rebuilt"
