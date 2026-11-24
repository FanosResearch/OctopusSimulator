#!/usr/bin/env bash
# banked.sh -- run the EEMBC traces on N-bank LLC presets and check every core completes.
#
#   test/banking/banked.sh <outdir> [banks...]          default banks: 2 4
#
# For each protocol family (snoop and directory MSI/MESI/MOESI), bank count and interleave
# (shift 0: lines alternate; 6: 4 KiB pages), the preset is derived with make_banked.py.
# A run passes when it exits 0 and every core's LatencyReport has one row per trace line
# plus the end-of-run "Average" footer. JOBS sets the parallelism; ARB overrides the arbiter;
# EXTRA="-p ..." adds settings to every run. PROPS=1 also traces every run and checks the banking
# properties of s8.3 with check_bank_trace.py (home, single, reached); the trace is deleted after.
# BMDIR=<dir> and BENCHES="a b" run other traces, e.g. the SPLASH subset (see splash_banked.sh). ONLY=<regex> selects families; DIR_MSI_L1, DIR_MSI_LLC, DIR_MESI_L1, DIR_MESI_LLC, DIR_MOESI_L1, DIR_MOESI_LLC pick the directory FSM tables.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$1"; shift; BANKS="${*:-2 4}"; JOBS="${JOBS:-16}"
BIN="$ROOT/build/Octopus_Simulator"; W="${BMDIR:-$ROOT/BMs/eembc-traces}"; C="$ROOT/configuration/SystemConfigurations"
EEMBC="${BENCHES:-a2time01-trace aifirf01-trace basefp01-trace cacheb01-trace empty-trace iirflt01-trace mi_pingpong pntrch01-trace rspeed01-trace ttsprk01-trace}"
p() { echo "-p cache_controller[*].protocol_type(s)=$1 -p cache_controller[*].fsm_filename(s)=$2 -p llc_controller[*].protocol_type(s)=$3 -p llc_controller[*].fsm_filename(s)=$4"; }
EXCL="-p cache_controller_type(s)=CacheControllerExclusive"
FAMILIES=(
  "snoop-msi|MultiCoreSystem_Snoop|"
  "snoop-mesi|MultiCoreSystem_Snoop|$EXCL $(p SNOOP_MESI MESI_splitBus_snooping SNOOP_LLC_MESI MESI_LLC)"
  "snoop-moesi|MultiCoreSystem_Snoop|$EXCL $(p SNOOP_MOESI MOESI_splitBus_snooping SNOOP_LLC_MOESI MOESI_LLC)"
  "dir-msi|MultiCoreSystem_Directory|$(p DIRECTORY_MSI ${DIR_MSI_L1:-MSI_directory} DIRECTORY_LLC_MSI ${DIR_MSI_LLC:-MSI_LLC_directory_v2})"
  "dir-mesi|MultiCoreSystem_Directory|$(p DIRECTORY_MESI ${DIR_MESI_L1:-MESI_directory_banked_v2} DIRECTORY_LLC_MESI ${DIR_MESI_LLC:-MESI_LLC_directory_v3})"
  "dir-moesi|MultiCoreSystem_Directory|$(p DIRECTORY_MOESI ${DIR_MOESI_L1:-MOESI_directory_v2} DIRECTORY_LLC_MOESI ${DIR_MOESI_LLC:-MOESI_LLC_directory_v2})"
)
[ -n "${ARB:-}" ] && ARBP="-p bus[0].interconnect_controller.arbiter_type(s)=$ARB" || ARBP=""
mkdir -p "$OUT/cfg"; : > "$OUT/jobs"
for f in "${FAMILIES[@]}"; do
  IFS='|' read -r fam base args <<< "$f"
  [[ "$fam" =~ ${ONLY:-.} ]] || continue
  for n in $BANKS; do for sh in 0 6; do
    cfg="$OUT/cfg/${base}_${n}B_s${sh}.csv"
    [ -f "$cfg" ] || python3 "$ROOT/test/banking/make_banked.py" "$C/$base.csv" "$cfg" "$n" "$sh"
    ids=$(grep '^llc_controller\[[0-9]*\]\.m_id' "$cfg" | cut -d, -f2 | paste -sd,)
    for b in $EEMBC; do echo "$fam-${n}B-s$sh|$b|$ids|$sh|-s MultiCoreSystem -c $cfg $args $ARBP ${EXTRA:-}" >> "$OUT/jobs"; done
  done; done
done
export BIN OUT W PROPS="${PROPS:-}" CHECK="$ROOT/test/banking/check_bank_trace.py"
xargs -P "$JOBS" -d '\n' -I{} bash -c '
  IFS="|" read -r name b ids sh args <<< "{}"; d="$OUT/$name/$b"; rm -rf "$d"; mkdir -p "$d"
  read -ra A <<< "$args"; [ -n "$PROPS" ] && A+=(--trace)
  timeout "${CAP:-1800}" "$BIN" "${A[@]}" -p "workload_path(s)=$W/$b/" -o "$d" > "$d/.stdout" 2> "$d/.stderr"; echo $? > "$d/.rc"
  if [ -n "$PROPS" ]; then python3 "$CHECK" "$d/trace.bin" --banks "$ids" --shift "$sh" > "$d/.props" 2>&1; echo $? > "$d/.props_rc"; rm -f "$d"/trace.bin*; fi' < "$OUT/jobs"
pass=0; fail=0; : > "$OUT/results.txt"
while IFS='|' read -r name b ids sh args; do
  d="$OUT/$name/$b"; rc=$(cat "$d/.rc"); ok=1; why=""
  [ "$rc" = 0 ] || { ok=0; why="rc=$rc"; }
  for t in "$W/$b"/trace_C*.trc.shared; do
    c=$(basename "$t" | sed 's/trace_C\([0-9]*\).*/\1/'); r="$d/LatencyReport_C$c.csv"
    want=$(( $(wc -l < "$t") + 1 ))
    [ -f "$r" ] && got=$(grep -c "^[0-9]" "$r") || got=0
    [ -f "$r" ] && grep -q Average "$r" || { ok=0; why="$why C$c:nofooter"; }
    [ "$got" = "$want" ] || { ok=0; why="$why C$c:$got/$want"; }
  done
  [ -z "${PROPS:-}" ] || [ "$(cat "$d/.props_rc" 2>/dev/null)" = 0 ] || { ok=0; why="$why props:$(grep -h FAIL "$d/.props" | cut -d' ' -f1 | tr -d : | paste -sd,)"; }
  if [ $ok = 1 ]; then pass=$((pass+1)); echo "PASS $name/$b" >> "$OUT/results.txt"
  else fail=$((fail+1)); echo "FAIL $name/$b $why $(head -c 200 "$d/.stderr" | tr '\n' ' ')" >> "$OUT/results.txt"; fi
done < "$OUT/jobs"
echo "$pass PASS, $fail FAIL ($OUT/results.txt)"
grep FAIL "$OUT/results.txt" | head -30
[ $fail = 0 ]
