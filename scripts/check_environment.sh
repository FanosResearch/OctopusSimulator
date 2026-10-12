#!/usr/bin/env bash
#
# check_environment.sh -- one line per component, then a short simulation whose
# numbers are fixed. Run it after creating a codespace, or on any machine you are
# about to use for the tutorial:
#
#   bash scripts/check_environment.sh
#
# The simulator is deterministic and, since the MCsim address-decode fix, produces
# identical results on Linux and Windows. So the last two numbers are not a smoke
# test of "did something run" -- they are an equality check against the reference.
# A mismatch is a real signal: a stale build, a modified configuration, or a real bug.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROOT="$PWD"

# The reference run: the single-core DRAM exerciser under the committed configuration.
# It runs MCsim rather than the fixed-latency memory on purpose: the DRAM model is the
# part that was platform-dependent, so it is the part worth checking.
WORKLOAD="$ROOT/demo/workloads/dramtest"
REF_REQUESTS=15364
REF_WORST_DRAM=377

fails=0
pass(){ printf '[PASS] %-14s %s\n' "$1" "$2"; }
fail(){ printf '[FAIL] %-14s %s\n' "$1" "$2"; fails=$((fails+1)); }

# --- tools -----------------------------------------------------------------------
if v=$(g++ --version 2>/dev/null | head -1); then pass compiler "$v"; else fail compiler "g++ not found"; fi
if v=$(cmake --version 2>/dev/null | head -1); then pass cmake "$v"; else fail cmake "cmake not found"; fi
if command -v xz >/dev/null 2>&1; then pass xz "$(xz --version 2>/dev/null | head -1)"; else fail xz "xz not found (xz-utils)"; fi

missing=""
for m in duckdb numpy matplotlib; do
  python3 -c "import $m" 2>/dev/null || missing="$missing $m"
done
if [ -z "$missing" ]; then pass python "duckdb, numpy, matplotlib"; else fail python "missing:$missing"; fi

# --- repository contents ---------------------------------------------------------
BIN="$ROOT/build/Octopus_Simulator"
[ -f "$BIN" ] || BIN="$ROOT/build/Octopus_Simulator.exe"
if [ -x "$BIN" ]; then pass octopus "${BIN#$ROOT/}"; else fail octopus "not built -- run: cmake -S . -B build && cmake --build build"; fi

# MCsim's .ini parser rejects a line that still carries a carriage return, aborting with
# "Malformed Line N (missing equals)". That happens when a tree checked out on Windows
# (core.autocrlf=true) is copied or exported to Linux. Catch it here, where it can say so.
ini="$ROOT/src/MCsim/system/FRFCFS/FRFCFS.ini"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) on_windows=1;; *) on_windows=0;; esac
if [ ! -f "$ini" ]; then
  fail mcsim-ini "missing $ini"
elif [ "$on_windows" -eq 0 ] && grep -q $'\r' "$ini"; then
  fail mcsim-ini "CRLF endings; fix: find src/MCsim -name '*.ini' -exec sed -i 's/\\r\$//' {} +"
else
  pass mcsim-ini "line endings usable"
fi

n_traces=$(ls -d "$ROOT"/BMs/eembc-traces/*-trace 2>/dev/null | wc -l)
if [ "$n_traces" -gt 0 ]; then pass benchmarks "BMs/eembc-traces ($n_traces traces)"; else fail benchmarks "no EEMBC traces -- run: ./get_benchmarks.sh"; fi

# --- the reference run -----------------------------------------------------------
if [ -x "$BIN" ] && [ -d "$WORKLOAD" ]; then
  rm -f "$WORKLOAD/newLogger"/*.csv 2>/dev/null
  mkdir -p "$WORKLOAD/newLogger"
  wpath=$(cygpath -m "$WORKLOAD" 2>/dev/null || printf '%s' "$WORKLOAD")
  if "$BIN" -s MultiCoreSystem -p "workload_path(s)=$wpath/" \
            -p "main_memory_type(s)=MCsim" -p "mcsim_scheduler(s)=FRFCFS" \
            > "$WORKLOAD/.out.check" 2>&1; then
    rep="$WORKLOAD/newLogger/LatencyReport_C0.csv"
    sum="$WORKLOAD/newLogger/Summary.csv"
    requests=$(awk 'END{print NR-2}' "$rep" 2>/dev/null)          # minus header and footer
    worst=$(awk -F, 'NR==2{print $8}' "$sum" 2>/dev/null)         # worst-case DRAM latency
    if [ "$requests" = "$REF_REQUESTS" ] && [ "$worst" = "$REF_WORST_DRAM" ]; then
      pass "sample run" "$requests requests, worst DRAM $worst cycles"
    else
      fail "sample run" "got $requests requests / worst DRAM $worst, expected $REF_REQUESTS / $REF_WORST_DRAM"
      # The commonest cause by far, and not a broken environment: the reference numbers
      # are for the configuration as committed, so any edit under configuration/ moves
      # them. That is expected once the exercises start editing the CSVs.
      if ! git -C "$ROOT" diff --quiet -- configuration 2>/dev/null; then
        printf '       (configuration/ differs from the committed baseline -- "git diff configuration" to see it)\n'
      fi
    fi
  else
    fail "sample run" "simulator exited non-zero (see $WORKLOAD/.out.check)"
  fi
fi

echo
if [ "$fails" -eq 0 ]; then
  echo "ALL CHECKS PASSED"
else
  echo "$fails CHECK(S) FAILED -- paste this output to the tutorial organisers"
  exit 1
fi
