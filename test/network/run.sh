#!/usr/bin/env bash
# Network test runner (docs/NoC_Test_Plan.md).
#   bash test/network/run.sh [step|all|reg|refs]
#     step  : "0".."8" runs that step's tests plus the regression suite
#     all   : every step so far plus the regression suite
#     reg   : the regression suite only
#     refs  : (re)generate the committed references under test/network/ref (step 0 only)
# Prints one "PASS <id>" / "FAIL <id>: measured <x> expected <y>" / "XFAIL <id>: <reason>" line
# per test, writes test/network/out/results.csv, exits non-zero on any FAIL.
# No user configuration file is edited: runs use committed presets and -p overrides,
# and the shipped MultiCoreSystem.csv is taken from the committed copy under ref/.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"; REF="$HERE/ref"; mkdir -p "$OUT"
RESULTS="$OUT/results.csv"
BIN="$ROOT/build/Octopus_Simulator"; [ -f "$BIN" ] || BIN="$ROOT/build/Octopus_Simulator.exe"
TESTS="$ROOT/build/test/network/unit/Octopus_tests"; [ -f "$TESTS" ] || TESTS="$TESTS.exe"
WHAT="${1:-all}"
FAILS=0

wpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
now() { date +%s.%N; }
elapsed() { awk -v a="$1" -v b="$(date +%s.%N)" "BEGIN{printf \"%.1f\", b-a}"; }
record() { # id status measured expected seconds
  printf '%s,%s,%s,%s,%s\n' "$1" "$2" "$3" "$4" "$5" >> "$RESULTS"
}
pass()  { echo "PASS $1"; record "$1" PASS "${2:-}" "${3:-}" "${4:-0}"; }
fail()  { echo "FAIL $1: measured ${2:-?} expected ${3:-?}"; record "$1" FAIL "${2:-}" "${3:-}" "${4:-0}"; FAILS=$((FAILS+1)); }
xfail() { echo "XFAIL $1: $2"; record "$1" XFAIL "" "$2" 0; }

# strip CR so Windows and Linux outputs compare equal
sum() { tr -d '\r' < "$1" | md5sum | cut -c1-32; }
sumdir() { # md5 over Summary.csv and LatencyReport_C*.csv of a run dir, in name order
  ( cd "$1" && for f in $(ls Summary.csv LatencyReport_C*.csv 2>/dev/null | sort); do printf '%s ' "$f"; sum "$f"; done ) | md5sum | cut -c1-32
}
# run the simulator: run <outdir> <args...>; returns the exit status, output in <outdir>
sim() {
  local out="$1"; shift
  rm -rf "$out"; mkdir -p "$out"
  "$BIN" "$@" -o "$(wpath "$out")" > "$out/.stdout" 2> "$out/.stderr"
}
EEMBC="a2time01-trace aifirf01-trace basefp01-trace cacheb01-trace empty-trace iirflt01-trace mi_pingpong pntrch01-trace rspeed01-trace ttsprk01-trace"
W() { wpath "$ROOT/BMs/eembc-traces/$1"; }
COMMITTED_CFG="$(wpath "$REF/MultiCoreSystem.committed.csv")"

build() {
  local t0; t0=$(now)
  if ! cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)" > "$OUT/build.log" 2>&1; then
    echo "BUILD FAILED, see $OUT/build.log"; tail -20 "$OUT/build.log"; exit 2
  fi
  echo "build ok ($(printf '%.0f' "$(elapsed "$t0")") s)"
}

# ---------------------------------------------------------------- regression suite
reg_bus() {
  local t0; t0=$(now)
  sim "$OUT/reg-bus" -s MultiCoreSystem -c "$COMMITTED_CFG" -p "workload_path(s)=$(W a2time01-trace)/"
  if diff -q <(tr -d '\r' < "$ROOT/tutorial/00-setup-and-first-run/expected/Summary.csv") <(tr -d '\r' < "$OUT/reg-bus/Summary.csv") > /dev/null 2>&1; then
    pass REG-BUS identical identical "$(elapsed "$t0")"
  else fail REG-BUS "Summary.csv differs" "tutorial/00 reference" "$(elapsed "$t0")"; fi
}
reg_env() {
  local t0; t0=$(now)
  local line; line=$(bash "$ROOT/scripts/check_environment.sh" 2>&1 | grep -i "sample run" | tail -1)
  if echo "$line" | grep -q "15364 requests" && echo "$line" | grep -q "377 cycles"; then
    pass REG-ENV "15364/377" "15364/377" "$(elapsed "$t0")"
  else fail REG-ENV "$line" "15364 requests, worst DRAM 377 cycles" "$(elapsed "$t0")"; fi
}
reg_mesh() {
  local t0; t0=$(now)
  local ok=1 detail=""
  for t in $EEMBC; do
    sim "$OUT/reg-mesh/$t" -s MultiCoreSystem_Mesh -p "workload_path(s)=$(W $t)/" &
  done; wait
  for t in $EEMBC; do
    local got; got=$(sumdir "$OUT/reg-mesh/$t")
    local want; want=$(cat "$REF/legacy-mesh/$t.md5" 2>/dev/null || echo "missing")
    [ "$got" = "$want" ] || { ok=0; detail="$detail $t"; }
  done
  if [ $ok = 1 ]; then pass REG-MESH identical identical "$(elapsed "$t0")"
  else fail REG-MESH "differs:$detail" "legacy-mesh references" "$(elapsed "$t0")"; fi
}
reg_tdm() {
  local t0; t0=$(now)
  sim "$OUT/reg-tdm" -s MultiCoreSystem -c "$COMMITTED_CFG" -p "workload_path(s)=$(W a2time01-trace)/" -p "bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter"
  local got; got=$(sumdir "$OUT/reg-tdm"); local want; want=$(cat "$REF/reg-tdm.md5" 2>/dev/null || echo missing)
  if [ "$got" = "$want" ]; then pass REG-TDM identical identical "$(elapsed "$t0")"
  else fail REG-TDM "$got" "$want" "$(elapsed "$t0")"; fi
}
regression() { reg_bus; reg_env; reg_mesh; reg_tdm; }

make_refs() {
  mkdir -p "$REF/legacy-mesh"
  for t in $EEMBC; do sim "$OUT/reg-mesh/$t" -s MultiCoreSystem_Mesh -p "workload_path(s)=$(W $t)/" & done; wait
  for t in $EEMBC; do sumdir "$OUT/reg-mesh/$t" > "$REF/legacy-mesh/$t.md5"; echo "$t $(cat "$REF/legacy-mesh/$t.md5") rows=$(wc -l < "$OUT/reg-mesh/$t/Summary.csv")"; done
  sim "$OUT/reg-tdm" -s MultiCoreSystem -c "$COMMITTED_CFG" -p "workload_path(s)=$(W a2time01-trace)/" -p "bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter"
  sumdir "$OUT/reg-tdm" > "$REF/reg-tdm.md5"; echo "reg-tdm $(cat "$REF/reg-tdm.md5")"
}

# ---------------------------------------------------------------- unit tests by prefix
unit() { # unit <prefix>
  local t0; t0=$(now)
  "$TESTS" "$1" | while IFS= read -r line; do
    case "$line" in
      PASS\ *) id=${line#PASS }; echo "$line"; record "$id" PASS "" "" 0;;
      FAIL\ *) id=${line#FAIL }; id=${id%%:*}; echo "$line"; record "$id" FAIL "${line#*: }" "" 0; echo FAILMARK >> "$OUT/.unitfail";;
      *) echo "$line";;
    esac
  done
  if [ -f "$OUT/.unitfail" ]; then FAILS=$((FAILS + $(wc -l < "$OUT/.unitfail"))); rm -f "$OUT/.unitfail"; fi
}

# ---------------------------------------------------------------- steps
step0() {
  [ -x "$TESTS" ] && pass S0-HARNESS || fail S0-HARNESS "no Octopus_tests" "built"
  [ -f "$RESULTS" ] && pass S0-RUNNER || fail S0-RUNNER "no results.csv" "written"
  [ -f "$REF/reg-tdm.md5" ] && [ -f "$REF/legacy-mesh/a2time01-trace.md5" ] && pass S0-REF || fail S0-REF "missing" "ref/*.md5"
  if [ -f "$ROOT/build_asan/Octopus_Simulator" ]; then pass S0-ASAN; else xfail S0-ASAN "ASan build not present on this machine (bash test/network/asan.sh builds it under WSL)"; fi
  for f in configuration/Interconnect/Network.csv configuration/SystemConfigurations/NetworkOnly.csv configuration/SystemConfigurations/MultiCoreSystem_Grid.csv; do
    [ -f "$ROOT/$f" ] || { fail S0-PRESETS "missing $f" "present"; return; }
  done; pass S0-PRESETS
}
for s in "$HERE"/steps/step*.sh; do [ -f "$s" ] && source "$s"; done

# ---------------------------------------------------------------- main
: > "$RESULTS"; echo "id,status,measured,expected,seconds" > "$RESULTS"
build
case "$WHAT" in
  refs) make_refs; exit 0;;
  reg)  regression;;
  all)  regression; for n in 0 1 2 3 4 5 6 7 8; do declare -f "step$n" > /dev/null && "step$n"; done;;
  [0-8]) regression; "step$WHAT";;
  *) echo "usage: run.sh [step|all|reg|refs]"; exit 2;;
esac
echo "----"; echo "$FAILS failure(s); results in $RESULTS"
[ "$FAILS" = 0 ]
