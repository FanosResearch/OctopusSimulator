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
#   - DRAM bank partitions (MCsim) confine each core's DRAM accesses to its banks with the placer on,
#     also with a colour partition on shared address bits and under another .ini mapping;
#   - an MSHR partition keeps a victim core's worst case near its run alone next to streamers,
#     and every core's peak entries within its units;
#   - a write-back buffer partition (requester mode) keeps every core's peak entries within its
#     units with no violation; victim mode counts;
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

# DRAM bank partitions (s6.9): units are MCsim's banks; MCsimInterface counts every DRAM access.
# The default FRFCFS .ini maps bank bits to 13-14 and bank-group bits to 15-16; the default LLC
# (256 sets) has 4 colours on bits 12-13, so colour and DRAM bank share bit 13 (joint placement).
MC="-p main_memory_type(s)=MCsim"
DB="-p $L.partition[0].scheme(s)=dram_bank -p $L.partition[0].owners(s)=0:0-3;1:4-7;2:8-11;3:12-15"
run dram-unplaced "$SN" "$PRIV" $MC $DB;                                         expect dram-bank-placer-off 0 "-gt 0" "= 0"
run dram-private  "$SN" "$PRIV" $MC $DB $PL;                                     expect dram-bank-placer-private 0 "= 0" "= 0"
run dram-group    "$SN" "$PRIV" $MC -p "$L.partition[0].scheme(s)=dram_bank_group" -p "$L.partition[0].owners(s)=0:0;1:1;2:2;3:3" $PL
                                                                                expect dram-bank-group-private 0 "= 0" "= 0"
# colour c has bit 13 = c >> 1, so core c can use only the two of its four banks with the same bit 13
run dram+colour   "$SN" "$PRIV" $MC $DB -p "$L.partition[1].scheme(s)=colour" -p "$L.partition[1].owners(s)=0:0;1:1;2:2;3:3" $PL
if [ "$RC" = 0 ] && grep -q "share address bits" "$D/out" && [ "$(grep -o 'pages on [0-9]*/[0-9]*' "$D/out" | sort -u)" = "pages on 2/2" ]
then expect dram-bank-and-colour-overlapping 0 "= 0" "= 0"
else bad "dram-bank-and-colour-overlapping: $(grep 'PagePlacer' "$D/out" | tr '\n' ' ')"; fi
run dram+colour-x "$SN" "$PRIV" $MC -p "$L.partition[0].scheme(s)=dram_bank" -p "$L.partition[0].owners(s)=0:1;0:3;1-3:4-15" \
                  -p "$L.partition[1].scheme(s)=colour" -p "$L.partition[1].owners(s)=0:0;1:1;2:2;3:3" $PL
                                                                                expect dram-bank-colour-disagree-refused 2 "" ""
# another .ini mapping (FCFS: AddressMapping=012345, banks above the rows) is followed
run dram-fcfs     "$SN" "$PRIV" $MC -p "mcsim_scheduler(s)=FCFS" $DB $PL;       expect dram-bank-fcfs-mapping 0 "= 0" "= 0"
run dram-fixed    "$SN" "$PRIV" $DB $PL;                                         expect dram-bank-needs-mcsim 2 "" ""

# MSHR partition (s6.10): a victim core (a read every 40 cycles, all LLC misses) next to three
# streamers (a new line every cycle) on an LLC with 16 MSHRs. Shared, the streamers hold the
# MSHRs and the victim's worst case grows about 7x; with 8 units of its own it stays near its
# run alone. Every core's peak entries stay within its units.
ST="$OUT/stream"; mkdir -p "$ST"
awk 'BEGIN { for (i = 0; i < 3000; i++) printf "%x 1 R %d\n", 268435456 + (i * 97 % 4096) * 64, 100 + i * 40 }' > "$ST/trace_C0.trc.shared"
for c in 1 2 3; do
  awk -v c=$c 'BEGIN { for (i = 0; i < 40000; i++) printf "%x 1 R %d\n", c * 2^36 + 536870912 + i * 64, 100 + i }' > "$ST/trace_C$c.trc.shared"
done
M16="-p llc_controller.num_mshr(i)=16"; MS="-p $L.partition[0].scheme(s)=mshr"
wc0() { tail -1 "$D/LatencyReport_C0.csv" | cut -d, -f9; }   # core 0's worst-case total latency
run mshr-none "$SN" "$ST" $M16; shared_wc=$(wc0)
run mshr-split "$SN" "$ST" $M16 $MS -p "$L.partition[0].owners(s)=0:0-7;1-3:8-15"
over=$(grep -o 'peak [0-9]* of [0-9]*' "$D/out" | awk '$2 > $4' | wc -l); np=$(grep -c 'peak [0-9]* of' "$D/out")
if [ "$RC" = 0 ] && [ "$np" = 4 ] && [ "$over" = 0 ] && [ "$(wc0)" -lt $((shared_wc / 2)) ]; then
  expect mshr-victim-isolated 0 "= 0" "= 0"; echo "     core 0 worst case $shared_wc shared, $(wc0) with 8 own MSHRs"
else bad "mshr-victim-isolated: worst case $shared_wc -> $(wc0), $np peaks, $over over their units"; fi
run mshr-unbounded "$SN" "$ST" -p "llc_controller.num_mshr(i)=-1" $MS -p "$L.partition[0].owners(s)=0:0";  expect mshr-unbounded-refused 2 "" ""

# Write-back buffer partition (s6.10): the same pattern with writes, so an evicted line is dirty in
# an L1 and holds its entry until the data comes back, on an 8-entry LLC buffer. Requester mode
# holds a core back while its free units do not exceed its queued requests plus its open misses:
# no violation, every core's peak within its units. Victim mode charges the core that placed the
# evicted line and only counts.
WS="$OUT/wstream"; mkdir -p "$WS"
awk 'BEGIN { for (i = 0; i < 3000; i++) printf "%x 1 W %d\n", 268435456 + (i * 97 % 4096) * 64, 100 + i * 40 }' > "$WS/trace_C0.trc.shared"
for c in 1 2 3; do
  awk -v c=$c 'BEGIN { for (i = 0; i < 40000; i++) printf "%x 1 W %d\n", c * 2^36 + 536870912 + i * 64, 100 + i }' > "$WS/trace_C$c.trc.shared"
done
P8="-p $L.pwb_size(i)=8"; PW="-p $L.partition[0].scheme(s)=pwb -p $L.partition[0].owners(s)=0:0-1;1-3:2-7"
peaks() { np=$(grep -c 'peak [0-9]* of' "$D/out"); over=$(grep -o 'peak [0-9]* of [0-9]*' "$D/out" | awk '$2 > $4' | wc -l); }
run pwb-req "$SN" "$WS" $P8 $PW; peaks
if [ "$np" = 4 ] && [ "$over" = 0 ]; then expect pwb-requester-bounded 0 "= 0" "= 0"
else bad "pwb-requester-bounded: $np peaks, $over over their units"; fi
run pwb-vic "$SN" "$WS" $P8 $PW -p "$L.partition[0].pwb_owner(s)=victim"; peaks
if [ "$np" = 4 ] && [ "$(col 5)" -gt 0 ]; then expect pwb-victim-counted 0 "-ge 0" "= 0"
else bad "pwb-victim-counted: $np peaks, $(col 5) fills"; fi
run pwb-owner-bad  "$SN" "$WS" $P8 $PW -p "$L.partition[0].pwb_owner(s)=owner";  expect pwb-owner-refused 2 "" ""
run pwb-unbounded  "$SN" "$WS" -p "$L.pwb_size(i)=-1" $PW;                        expect pwb-unbounded-refused 2 "" ""
[ $fails = 0 ] && echo "partition: all PASS" || echo "partition: $fails FAIL"
exit $fails
