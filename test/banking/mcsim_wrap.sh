#!/usr/bin/env bash
# mcsim_wrap.sh -- MCsim must keep issuing DRAM commands past DRAM cycle 2^32.
#
#   test/banking/mcsim_wrap.sh [binary] [outdir]
#
# MCsim's DRAM device clock was 32-bit: at cycle 2^32 it wrapped to 0, every Ramulator timing
# check "clk < next[cmd]" failed from then on, and no command issued again, so the whole system
# hung (a gem5 full-system run with ATP aggressors stopped at core cycle 2^33). Reaching 2^32
# takes hours, so OCTOPUS_MCSIM_DEVICE_START_CYCLE starts the device clock 20000 cycles before
# it. Each EEMBC trace below then crosses the wrap early in the run; with MCsim it must finish
# (every LatencyReport has its Average footer) within the timeout. Exits non-zero on a hang.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${1:-$ROOT/build/Octopus_Simulator}"; OUT="${2:-$(mktemp -d)}"
START=$(( (1 << 32) - 20000 )); fails=0
for b in a2time01-trace cacheb01-trace iirflt01-trace; do
  d="$OUT/$b"; rm -rf "$d"; mkdir -p "$d"
  OCTOPUS_MCSIM_DEVICE_START_CYCLE=$START timeout 300 "$BIN" -s MultiCoreSystem -c MultiCoreSystem_Snoop \
      -p "main_memory_type(s)=MCsim" -p "workload_path(s)=$ROOT/BMs/eembc-traces/$b/" -o "$d" > "$d/out" 2>&1
  rc=$? ok=1
  for f in "$d"/LatencyReport_C*.csv; do grep -q Average "$f" 2>/dev/null || ok=0; done
  if [ $rc = 0 ] && [ $ok = 1 ]; then echo "PASS $b: finished past DRAM cycle 2^32"
  else echo "FAIL $b: exit $rc$([ $rc = 124 ] && echo ' (timeout: hung)'), reports complete=$ok"; fails=$((fails + 1)); fi
done
[ $fails = 0 ] && echo "mcsim_wrap: all PASS" || echo "mcsim_wrap: $fails FAIL"
exit $fails
