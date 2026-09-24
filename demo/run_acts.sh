#!/usr/bin/env bash
# run_acts.sh -- the three acts of the real-time tracking demo, each a cycle-accurate Octopus
# run of the periodic localization task (docs/Tasks.md) on core 0:
#
#   act1  alone       core 0 only (cores 1-3 idle), FCFS bus
#   act2  contention  core 0 + three streaming aggressors, FCFS bus
#   act3  protected   same load, Round-Robin bus + one LLC way reserved for core 0
#                     (llc_controller.m_data_handler.way_partition = "0:0")
#
# Shared config = the paper's PCC-like setup written the way the sweeps write it (snoop MESI +
# Exclusive L1, MCsim DDR4 with FRFCFS); restored on exit. Each act writes
#   demo/workloads/<act>/newLogger/{JobReport_C0.csv, LatencyReport_C*.csv, Summary.csv}
# and extract_jobs.py turns them into demo/data/act<n>.jsonl (+ .manifest.json) for the page.
#
# Usage: bash demo/run_acts.sh [act1|act2|act3 ...]     (default: all three)
# Env:   CYCLE_NS (display scale for the page readouts, default 1.0), PY (python)
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../sweeps" && pwd)/sweep_common.sh"
DEMO="$SWEEP_ROOT/demo"; DATA="$DEMO/data"; mkdir -p "$DATA"
CYCLE_NS="${CYCLE_NS:-1.0}"; PY="${PY:-python}"

_sbc_bak="$(mktemp)"; cp "$SBC" "$_sbc_bak"; _cfg_bak="$(mktemp)"; cp "$CFG" "$_cfg_bak"
trap 'cp "$_sbc_bak" "$SBC"; cp "$_cfg_bak" "$CFG"; rm -f "$_sbc_bak" "$_cfg_bak"' EXIT
gen_baseline
set_csv 'llc_controller\.arbiter_type\(s\),'  "llc_controller.arbiter_type(s),RRArbiter,,,,,"
set_csv 'main_memory_type\(s\),'              "main_memory_type(s),MCsim,,,,,"
set_csv 'mcsim_scheduler\(s\),'               "mcsim_scheduler(s),FRFCFS,,,,,"

run_act(){
  local act="$1" wl="$2" arb="$3" part="$4" label="$5"
  local d="$DEMO/workloads/$wl"
  set_arbiter "$arb"
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv
  echo "== $act: $label"
  echo "   config: $(grep -E '^arbiter_type' "$SBC" | cut -d, -f2) bus arbiter, system csv $(md5sum "$CFG" | cut -c1-8), partition='$part'"
  local s=$(date +%s)
  "$BIN" -s MultiCoreSystem -p "workload_path(s)=$(cygpath -m "$d" 2>/dev/null || echo "$d")/" \
         -p "llc_controller.m_data_handler.way_partition(s)=$part" > "$d/.out.$act" 2>&1 || { tail -3 "$d/.out.$act"; echo "   FAILED"; return 1; }
  grep -iq "cannot\|fault" "$d/.out.$act" && { grep -i "cannot\|fault" "$d/.out.$act" | head -2; echo "   FAILED"; return 1; }
  echo "   $(( $(date +%s) - s )) s"
  "$PY" "$DEMO/extract_jobs.py" "$d/newLogger" --cycle-ns "$CYCLE_NS" --label "$label" -o "$DATA/$act.jsonl"
}

acts="${*:-act1 act2 act3}"
for a in $acts; do
  case "$a" in
    act1) run_act act1 alone      FCFSArbiter ""    "alone: core 0 only, FCFS bus";;
    act2) run_act act2 contention FCFSArbiter ""    "contention: 3 streaming cores, FCFS bus";;
    act3) run_act act3 contention RRArbiter   "0:0" "protected: RR bus + LLC way reserved for core 0";;
    *) echo "unknown act $a" >&2; exit 1;;
  esac
done
"$PY" "$DEMO/plot_trajectory.py" --data "$DATA" --out "$DEMO/figures" && "$PY" "$DEMO/build_page.py" --data "$DATA"
echo "-> $DATA, $DEMO/figures/trajectory.pdf, page rebuilt"
