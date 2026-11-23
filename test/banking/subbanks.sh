#!/usr/bin/env bash
# subbanks.sh -- directed check of LLC data-array sub-banks (docs/LLC_Banking_and_Partitioning_Design.md s5.4, B7).
#
#   test/banking/subbanks.sh [outdir]
#
# Core 0 reads three lines into the LLC; at cycle 5000 cores 1-3 each read one of them, so the
# LLC serves three hits that reach it 2 cycles apart. With LLC data-access latency L = 50, an
# access waits only for an earlier access in the SAME sub-bank: the k-th access to one sub-bank
# waits k*L minus its arrival offset (0, 48, 96); accesses to different sub-banks wait 0.
# The banked case checks that a bank numbers sub-banks in its own (bank-stripped) line space:
# lines 0, 2, 4 all live in bank 0 of 2, become 0, 1, 2 there, and fall in sub-banks 0, 1, 0.
# Prints one line per case and exits non-zero on any mismatch.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$(mktemp -d)}"; BIN="$ROOT/build/Octopus_Simulator"; C="$ROOT/configuration/SystemConfigurations"
mkdir -p "$OUT"; fails=0

trace() {  # dir addr0 addr1 addr2
  local d="$1"; shift; mkdir -p "$d"
  printf "%x 1 R 10\n%x 1 R 2000\n%x 1 R 4000\n" "$@" > "$d/trace_C0.trc.shared"
  local a=("$@"); for c in 1 2 3; do printf "%x 1 R 5000\n" "${a[$((c-1))]}" > "$d/trace_C$c.trc.shared"; done
}
check() {  # name config llc-key sub_banks tracedir expected-waits
  local name="$1" cfg="$2" key="$3" sb="$4" tr="$5" want="$6" d="$OUT/run_$1"
  mkdir -p "$d"
  "$BIN" -s MultiCoreSystem -c "$cfg" -p "$key.m_data_handler.m_data_access_latency(i)=50" \
         -p "$key.m_data_handler.sub_banks(i)=$sb" -p "workload_path(s)=$tr/" -o "$d" > "$d/out" 2>&1
  local got; got=$(for c in 1 2 3; do awk -F, '$1 ~ /^[0-9]+$/ {print $8; exit}' "$d/LatencyReport_C$c.csv"; done | tr '\n' ' ')
  got="${got% }"
  if [ "$got" = "$want" ]; then echo "PASS $name: L2 access waits $got"
  else echo "FAIL $name: L2 access waits '$got', expected '$want'"; fails=$((fails+1)); fi
}

trace "$OUT/same" $((0x100000)) $((0x100100)) $((0x100200))   # lines 4 apart: one sub-bank of 4
trace "$OUT/diff" $((0x100000)) $((0x100040)) $((0x100080))   # consecutive lines: three sub-banks
trace "$OUT/bank" $((0x100000)) $((0x100080)) $((0x100100))   # lines 0, 2, 4: all in bank 0 of 2
python3 "$ROOT/test/banking/make_banked.py" "$C/MultiCoreSystem_Snoop.csv" "$OUT/Snoop_2B_s0.csv" 2 0

check one-subbank-same  MultiCoreSystem_Snoop llc_controller 1 "$OUT/same" "0 48 96"
check one-subbank-diff  MultiCoreSystem_Snoop llc_controller 1 "$OUT/diff" "0 48 96"
check four-subbank-same MultiCoreSystem_Snoop llc_controller 4 "$OUT/same" "0 48 96"
check four-subbank-diff MultiCoreSystem_Snoop llc_controller 4 "$OUT/diff" "0 0 0"
check 2bank-1subbank    "$OUT/Snoop_2B_s0.csv" "llc_controller[*]" 1 "$OUT/bank" "0 48 96"
check 2bank-2subbank    "$OUT/Snoop_2B_s0.csv" "llc_controller[*]" 2 "$OUT/bank" "0 0 46"
[ $fails = 0 ] && echo "subbanks: all PASS" || echo "subbanks: $fails FAIL"
exit $fails
