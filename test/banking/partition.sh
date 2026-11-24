#!/usr/bin/env bash
# partition.sh -- directed checks of LLC partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6).
#
#   test/banking/partition.sh [outdir]
#
# Runs cacheb01 on the snoop preset with way, colour and bank partitions and checks, from each
# run's Partitions.csv and exit code:
#   - way_partition(s) and partition[0] = way give identical reports;
#   - a colour partition with the page placer off is violated (addresses are unplaced), and
#     strict(i) stops the run;
#   - with the placer on there are no violations: shared traces (all cores touch the same
#     pages) only fill other cores' colours through pages placed for those cores (SharedOutside);
#     private traces (each core's addresses offset by core * 2^36) fill only their own colours,
#     on one LLC and on 2 and 4 banks;
#   - bank partitions (4 page-interleaved banks) confine each core's private pages to its banks;
#   - configurations that cannot work are refused at startup (exit code 2).
# Completed runs must have every LatencyReport footer. Prints one line per case; exits non-zero
# on any failure.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$(mktemp -d)}"; BIN="$ROOT/build/Octopus_Simulator"; C="$ROOT/configuration/SystemConfigurations"
SHARED="$ROOT/BMs/eembc-traces/cacheb01-trace"; PRIV="$OUT/private"
mkdir -p "$OUT" "$PRIV"; fails=0
for c in 0 1 2 3; do
  awk -v c=$c '{ printf "%x %s %s %s\n", strtonum("0x"$1) + c * 2^36, $2, $3, $4 }' "$SHARED/trace_C$c.trc.shared" > "$PRIV/trace_C$c.trc.shared"
done
for nb in 2 4; do for sh in 0 6; do
  python3 "$ROOT/test/banking/make_banked.py" "$C/MultiCoreSystem_Snoop.csv" "$OUT/Snoop_${nb}B_s$sh.csv" $nb $sh > /dev/null
done; done

run() {  # name config workload args... -> sets RC, D
  local name="$1" cfg="$2" wl="$3"; shift 3; D="$OUT/run_$name"; rm -rf "$D"; mkdir -p "$D"
  "$BIN" -s MultiCoreSystem -c "$cfg" -p "workload_path(s)=$wl/" -o "$D" "$@" > "$D/out" 2>&1; RC=$?
}
complete() { local f; for f in "$D"/LatencyReport_C*.csv; do grep -q Average "$f" || return 1; done; }
col() { awk -F, -v k="$1" 'NR > 1 { s += $k } END { print s + 0 }' "$D/Partitions.csv" 2>/dev/null || echo -1; }   # 5 fills, 6 violations, 7 shared
ok()   { echo "PASS $1"; }
bad()  { echo "FAIL $1"; fails=$((fails + 1)); }
expect() {  # name rc-wanted violations-test shared-test
  local name="$1" want_rc="$2" vtest="$3" stest="$4" v s
  if [ "$RC" != "$want_rc" ]; then bad "$name: exit $RC, expected $want_rc ($(grep -v '^-p' "$D/out" | tail -1))"; return; fi
  if [ "$want_rc" != 0 ]; then ok "$name: refused ($(tail -1 "$D/out" | sed 's/.*: //' | cut -c1-90))"; return; fi
  complete || { bad "$name: a core did not finish"; return; }
  v=$(col 6); s=$(col 7)
  if [ "$v" $vtest ] && [ "$s" $stest ]; then ok "$name: fills $(col 5), violations $v, shared-outside $s"
  else bad "$name: violations $v (want $vtest), shared-outside $s (want $stest)"; fi
}

L=llc_controller.m_data_handler; LB="llc_controller[*].m_data_handler"
SN="$C/MultiCoreSystem_Snoop.csv"
COL="-p $L.partition[0].scheme(s)=colour -p $L.partition[0].owners(s)=0:0;1-3:1-3"
COLB="-p $LB.partition[0].scheme(s)=colour -p $LB.partition[0].owners(s)=0:0;1-3:1-3"
PL="-p page_placer(i)=1"

run way-alias "$SN" "$SHARED" -p "$L.way_partition(s)=0:0;1-3:1"; a=$(cat "$D"/LatencyReport_C*.csv | md5sum)
run way-new   "$SN" "$SHARED" -p "$L.partition[0].scheme(s)=way" -p "$L.partition[0].owners(s)=0:0;1-3:1"
[ "$a" = "$(cat "$D"/LatencyReport_C*.csv | md5sum)" ] && expect way-alias-equals-partition0 0 "= 0" "= 0" || bad "way alias and partition[0] differ"

run colour-unplaced "$SN" "$SHARED" $COL;                                       expect colour-placer-off 0 "-gt 0" "= 0"
run colour-strict   "$SN" "$SHARED" $COL -p "$L.partition[0].strict(i)=1";        expect colour-strict-placer-off 2 "" ""
run colour-shared   "$SN" "$SHARED" $COL $PL;                                   expect colour-placer-shared 0 "= 0" "-gt 0"
run colour-private  "$SN" "$PRIV"   $COL $PL;                                   expect colour-placer-private 0 "= 0" "= 0"
for b in 2B_s0 2B_s6 4B_s0; do
  run colour-$b "$OUT/Snoop_$b.csv" "$PRIV" $COLB $PL;                          expect colour-placer-private-$b 0 "= 0" "= 0"
done
run colour-4B_s6 "$OUT/Snoop_4B_s6.csv" "$PRIV" $COLB $PL;                     expect colour-no-colours-refused 2 "" ""
run no-unit "$SN" "$SHARED" -p "$L.way_partition(s)=0:9";                       expect core-owning-nothing-refused 2 "" ""

if [ -f "$ROOT/src/Partitioning/LlcBankPartition.cpp" ]; then
  BNK="-p $LB.partition[0].scheme(s)=bank -p $LB.partition[0].owners(s)=0:0;1-3:1-3"
  run bank-unplaced "$OUT/Snoop_4B_s6.csv" "$PRIV" $BNK;                        expect bank-placer-off 0 "-gt 0" "= 0"
  run bank-private  "$OUT/Snoop_4B_s6.csv" "$PRIV" $BNK $PL;                    expect bank-placer-private 0 "= 0" "= 0"
  run bank-shared   "$OUT/Snoop_4B_s6.csv" "$SHARED" $BNK $PL;                  expect bank-placer-shared 0 "= 0" "-gt 0"
  run bank-line     "$OUT/Snoop_4B_s0.csv" "$PRIV" $BNK $PL;                    expect bank-line-interleave-refused 2 "" ""
  run bank-1llc     "$SN" "$PRIV" -p "$L.partition[0].scheme(s)=bank" -p "$L.partition[0].owners(s)=0:0";  expect bank-single-llc-refused 2 "" ""
  run bank+colour   "$OUT/Snoop_2B_s6.csv" "$PRIV" -p "$LB.partition[0].scheme(s)=bank" -p "$LB.partition[0].owners(s)=0-1:0;2-3:1" \
                    -p "$LB.partition[1].scheme(s)=colour" -p "$LB.partition[1].owners(s)=0:0;1:1;2:0;3:1" $PL
                                                                                expect bank-and-colour-together 0 "= 0" "= 0"
  # every core owns both banks and both colours: its pages must cover all 4 (bank, colour) pairs
  # (lockstep per-scheme round-robin covered only (0,0) and (1,1), half its capacity)
  run bank-x-colour "$OUT/Snoop_2B_s6.csv" "$PRIV" -p "$LB.partition[0].scheme(s)=bank" -p "$LB.partition[0].owners(s)=0-3:0-1" \
                    -p "$LB.partition[1].scheme(s)=colour" -p "$LB.partition[1].owners(s)=0-3:0-1" $PL
  if [ "$RC" = 0 ] && [ "$(grep -o 'pages on [0-9]*/[0-9]*' "$D/out" | sort -u)" = "pages on 4/4" ]; then expect bank-x-colour-all-pairs 0 "= 0" "= 0"
  else bad "bank-x-colour-all-pairs: $(grep 'pages placed' "$D/out")"; fi
fi
[ $fails = 0 ] && echo "partition: all PASS" || echo "partition: $fails FAIL"
exit $fails
