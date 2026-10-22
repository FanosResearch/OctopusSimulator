#!/usr/bin/env bash
# TileLink adapter tests (docs/TileLink_Plan.md s5.2, s5.3 I-A, step T7 infrastructure).
#   bash test/tilelink/run.sh [unit|sys|all]
#     unit : Octopus_tl_tests (TLPort through the crossbar with the echo stand-in, the admission
#            contract in CacheController, the Logger's TileLink columns) and the untagged-push fatal
#     sys  : MultiCoreSystem_TileLink elaboration: the refusals (one clock per kernel, one LLC) and a
#            run whose first L1 message reaches its TLPort (the directory tables, which carry no
#            TileLink header, stand in for the TL-C handlers and must be refused there)
#     all  : both (default)
# Prints one "PASS <id>" / "FAIL <id>: measured <x> expected <y>" line per test, writes
# test/tilelink/out/results.csv, exits non-zero on any FAIL. No configuration file is edited: runs
# use the committed preset and -p overrides.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"; mkdir -p "$OUT"
RESULTS="$OUT/results.csv"
BIN="$ROOT/build/Octopus_Simulator"; [ -f "$BIN" ] || BIN="$ROOT/build/Octopus_Simulator.exe"
TESTS="$ROOT/build/test/tilelink/unit/Octopus_tl_tests"; [ -f "$TESTS" ] || TESTS="$TESTS.exe"
WHAT="${1:-all}"
FAILS=0

wpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
record() { printf '%s,%s,%s,%s\n' "$1" "$2" "$3" "$4" >> "$RESULTS"; }
pass() { echo "PASS $1"; record "$1" PASS "${2:-}" "${3:-}"; }
fail() { echo "FAIL $1: measured ${2:-?} expected ${3:-?}"; record "$1" FAIL "${2:-}" "${3:-}"; FAILS=$((FAILS+1)); }
sim() { # sim <outdir> <args...>: runs the simulator, output in <outdir>/.stdout/.stderr, returns its status
  local out="$1"; shift
  rm -rf "$out"; mkdir -p "$out"
  timeout 300 "$BIN" "$@" -o "$(wpath "$out")" > "$out/.stdout" 2> "$out/.stderr"
}

build() {
  if ! cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)" > "$OUT/build.log" 2>&1; then
    echo "BUILD FAILED, see $OUT/build.log"; tail -20 "$OUT/build.log"; exit 2
  fi
  echo "build ok"
}

unit() {
  rm -f "$OUT/tl_unit_trace.bin"
  ( cd "$OUT" && TL_TEST_OUT="$OUT" OCTOPUS_TRACE="$OUT/tl_unit_trace.bin" "$TESTS" 2> "$OUT/unit.stderr" ) | while IFS= read -r line; do
    case "$line" in
      PASS\ *) echo "$line"; record "${line#PASS }" PASS "" "";;
      FAIL\ *) echo "$line"; id=${line#FAIL }; record "${id%%:*}" FAIL "${line#*: }" ""; echo x >> "$OUT/.unitfail";;
    esac
  done
  if [ -f "$OUT/.unitfail" ]; then FAILS=$((FAILS + $(wc -l < "$OUT/.unitfail"))); rm -f "$OUT/.unitfail"; fi
  # s5.2: pushMessage of a message without a TileLink channel is fatal (status 2, named)
  local st msg
  ( cd "$OUT" && TL_TEST_OUT="$OUT" "$TESTS" --fatal-channel0 > "$OUT/fatal.stdout" 2> "$OUT/fatal.stderr" ); st=$?
  msg=$(grep -c "no TileLink channel" "$OUT/fatal.stderr")
  if [ "$st" = 2 ] && [ "$msg" -ge 1 ]; then pass TL-PORT-FATAL-CH0 "exit 2, named" "exit 2, named"
  else fail TL-PORT-FATAL-CH0 "exit $st, message $msg" "exit 2 naming the missing channel"; fi
}

# the directory tables in place of the TL-C handlers (which T7's handler step adds)
DIR_TABLES=(-p "cache_controller[*].protocol_type(s)=DIRECTORY_MSI" -p "cache_controller[*].fsm_filename(s)=MSI_directory_v3"
            -p "llc_controller.protocol_type(s)=DIRECTORY_LLC_MSI" -p "llc_controller.fsm_filename(s)=MSI_LLC_directory")
sys_expect() { # sys_expect <id> <status> <stderr pattern> <args...>
  local id="$1" want="$2" pat="$3"; shift 3
  sim "$OUT/$id" -s MultiCoreSystem_Mesh -c MultiCoreSystem_TileLink "${DIR_TABLES[@]}" \
      -p "workload_path(s)=$(wpath "$ROOT/BMs/eembc-traces/a2time01-trace")/" "$@"
  local st=$?
  if [ "$st" = "$want" ] && grep -q "$pat" "$OUT/$id/.stderr"; then pass "$id" "exit $st" "exit $want, '$pat'"
  else fail "$id" "exit $st: $(tail -1 "$OUT/$id/.stderr")" "exit $want, '$pat'"; fi
}
sys() {
  # elaboration passed (clock check, kernel, monitors) and the first L1 message reached its TLPort
  sys_expect TL-SYS-WIRED 2 "port 0 (port0): pushMessage of a message with no TileLink channel"
  # plan s3.2: one clock per kernel
  sys_expect TL-SYS-CLOCK 2 "one clock per kernel" -p "llc_controller.m_clk_period(i)=200"
  # plan s5.3 I-A: one LLC on the crossbar
  sys_expect TL-SYS-BANKS 2 "takes one LLC" -p "llc_banks(i)=2"
}

echo "id,status,measured,expected" > "$RESULTS"
build
case "$WHAT" in
  unit) unit;;
  sys)  sys;;
  all)  unit; sys;;
  *) echo "usage: run.sh [unit|sys|all]"; exit 2;;
esac
echo "----"; echo "$FAILS failure(s); results in $RESULTS"
[ "$FAILS" = 0 ]
