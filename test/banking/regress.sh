#!/usr/bin/env bash
# regress.sh -- byte-identity regression for the LLC banking / partitioning work.
#
#   test/banking/regress.sh ref <binary> <dir>    run the matrix with <binary>, write <dir>/sums.txt
#   test/banking/regress.sh cmp <refdir> <dir>    run with build/Octopus_Simulator and compare
#
# Each run's fingerprint is the md5 of Summary.csv and LatencyReport_C*.csv. The matrix covers
# the snoop and directory protocol families, the bus arbiters, MCsim, perfect LLC and LLC way
# partitioning, on the EEMBC traces (BMs/eembc-traces). JOBS sets the parallelism.
# The simulator can stop early with exit code 0, so a fingerprint alone does not show a run
# finished: runs whose LatencyReports lack the end-of-run "Average" footer are listed in
# <dir>/incomplete.txt and counted in the final line.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="$1"; JOBS="${JOBS:-16}"
W="$ROOT/BMs/eembc-traces"
EEMBC="a2time01-trace aifirf01-trace basefp01-trace cacheb01-trace empty-trace iirflt01-trace mi_pingpong pntrch01-trace rspeed01-trace ttsprk01-trace"
SHORT="a2time01-trace cacheb01-trace iirflt01-trace"
SN="-s MultiCoreSystem -c MultiCoreSystem_Snoop"
DI="-s MultiCoreSystem -c MultiCoreSystem_Directory"
p() { echo "-p cache_controller[*].protocol_type(s)=$1 -p cache_controller[*].fsm_filename(s)=$2 -p llc_controller.protocol_type(s)=$3 -p llc_controller.fsm_filename(s)=$4"; }
EXCL="-p cache_controller_type(s)=CacheControllerExclusive"
# name|benches|args
CONFIGS=(
  "snoop-msi|$EEMBC|$SN"
  "snoop-mesi|$EEMBC|$SN $EXCL $(p SNOOP_MESI MESI_splitBus_snooping SNOOP_LLC_MESI MESI_LLC)"
  "snoop-moesi|$EEMBC|$SN $EXCL $(p SNOOP_MOESI MOESI_splitBus_snooping SNOOP_LLC_MOESI MOESI_LLC)"
  "dir-msi|$EEMBC|$DI"
  "dir-mesi|$EEMBC|$DI $(p DIRECTORY_MESI MESI_directory DIRECTORY_LLC_MESI MESI_LLC_directory)"
  "dir-moesi|$EEMBC|$DI $(p DIRECTORY_MOESI MOESI_directory DIRECTORY_LLC_MOESI MOESI_LLC_directory)"
  "default|$SHORT|-s MultiCoreSystem -c MultiCoreSystem"
  "snoop-rr|$SHORT|$SN -p bus[0].interconnect_controller.arbiter_type(s)=RRArbiter"
  "snoop-tdm|$SHORT|$SN -p bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter"
  "snoop-mcsim|$SHORT|$SN -p main_memory_type(s)=MCsim"
  "snoop-perfect|$SHORT|$SN -p llc_controller.perfect_llc(i)=1"
  "snoop-waypart|$SHORT|$SN -p llc_controller.m_data_handler.way_partition(s)=0:0;1-3:1"
  "snoop-waypart2|$SHORT|$SN -p llc_controller.m_data_handler.way_partition(s)=0-1:0;2:1"
)
fp() { ( cd "$1" && for f in $(ls Summary.csv LatencyReport_C*.csv 2>/dev/null | sort); do printf '%s ' "$f"; md5sum < "$f" | cut -c1-32; done ) | md5sum | cut -c1-32; }
run_matrix() {  # bin outdir
  local bin="$1" out="$2"; mkdir -p "$out"; : > "$out/jobs"
  for c in "${CONFIGS[@]}"; do
    IFS='|' read -r name benches args <<< "$c"
    for b in $benches; do echo "$name|$b|$args" >> "$out/jobs"; done
  done
  export bin out W
  xargs -P "$JOBS" -d '\n' -I{} bash -c '
    IFS="|" read -r name b args <<< "{}"; d="$out/$name/$b"; rm -rf "$d"; mkdir -p "$d"
    read -ra A <<< "$args"
    timeout 1800 "$bin" "${A[@]}" -p "workload_path(s)=$W/$b/" -o "$d" > "$d/.stdout" 2> "$d/.stderr"; echo $? > "$d/.rc"' < "$out/jobs"
  : > "$out/sums.txt"; : > "$out/incomplete.txt"
  while IFS='|' read -r name b args; do
    d="$out/$name/$b"; echo "$name/$b rc=$(cat $d/.rc) $(fp $d)" >> "$out/sums.txt"
    local ok=1 f; ls "$d"/LatencyReport_C*.csv > /dev/null 2>&1 || ok=0
    for f in "$d"/LatencyReport_C*.csv; do [ -f "$f" ] && ! grep -q Average "$f" && ok=0; done
    [ $ok = 1 ] || echo "$name/$b" >> "$out/incomplete.txt"
  done < "$out/jobs"
}
incomplete() { local n; n=$(wc -l < "$1/incomplete.txt"); [ "$n" = 0 ] || echo "WARNING: $n run(s) did not complete (no Average footer): $1/incomplete.txt"; }
case "$MODE" in
  ref) run_matrix "$2" "$3"; echo "$(wc -l < "$3/sums.txt") runs -> $3/sums.txt"; incomplete "$3";;
  cmp) run_matrix "$ROOT/build/Octopus_Simulator" "$3"; incomplete "$3"
       if diff "$2/sums.txt" "$3/sums.txt" > "$3/diff.txt"; then echo "IDENTICAL: $(wc -l < "$3/sums.txt") runs"
       else echo "DIFFERENT: $(grep -c '^>' "$3/diff.txt") of $(wc -l < "$3/sums.txt") runs"; cat "$3/diff.txt"; exit 1; fi;;
  *) echo "usage: $0 ref <binary> <dir> | cmp <refdir> <dir>"; exit 2;;
esac
