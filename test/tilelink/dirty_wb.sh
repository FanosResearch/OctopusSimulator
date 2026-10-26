#!/usr/bin/env bash
# DW-9/DW-10 (docs/TileLink_LLC_Dirty_Writeback_Plan.md): Octopus's TL-C LLC with the v1 and v2 manager tables, on the
# eviction stress of test/tilelink/run.sh oct-evict (a small LLC, a one-entry write-back buffer, shallow ports), the ten
# EEMBC benchmarks, MESI and MSI, one bank, two banks, and memory over TileLink. Every diagnostic on; TLC_LLC checks the
# generic dirty bit against the TL-C events' view at every memory write it considers (in v1 and v2).
# Per batch: the runner's verdict (4/4 cores complete, exit 0, no Fault, 0 monitor violations, hazard 0/0, tiling ok),
# and the accounting: v1 replacements = write-backs = memory writes; v2 replacements = write-backs + clean evictions,
# write-backs = memory writes. Prints one line per batch, then the v2/v1 memory writes.
# Usage: bash test/tilelink/dirty_wb.sh [outdir]
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/test/tilelink/out/dirty-wb}
BIN="$ROOT/build/Octopus_Simulator"
EEMBC="a2time01-trace aifirf01-trace basefp01-trace cacheb01-trace empty-trace iirflt01-trace mi_pingpong pntrch01-trace rspeed01-trace ttsprk01-trace"
Q=(-p "interconnect.rx_depth(i)=1" -p "interconnect.tx_depth(i)=2" -p "interconnect.tx_depth_B(i)=4" -p "cache_controller[*].m_data_handler.pwb_size(i)=1")

verdict() { # verdict <run dir> <benchmark>: ok, or what failed (test/tilelink/run.sh oct_verdict)
  local o="$1" t="$2" c refs rows fin
  for c in 0 1 2 3; do
    refs=$(wc -l < "$ROOT/BMs/eembc-traces/$t/trace_C$c.trc.shared"); rows=$(awk 'END{print NR}' "$o/LatencyReport_C$c.csv" 2>/dev/null)
    fin=$(grep -c "Average Latency" "$o/LatencyReport_C$c.csv" 2>/dev/null)
    { [ "${fin:-0}" -ge 1 ] && [ "${rows:-0}" -ge "$refs" ]; } || { echo "core $c unfinished"; return; }
  done
  [ "$(cat "$o/.rc")" = 0 ] || { echo "exit $(cat "$o/.rc")"; return; }
  ! grep -q "Fault\|dirty bit" "$o/.stdout" "$o/.stderr" || { echo "fault: $(grep -h -m1 'Fault\|dirty bit' "$o/.stdout" "$o/.stderr" | cut -c1-160)"; return; }
  grep -q "link monitors, 0 violations" "$o/.stderr" || { echo "monitor"; return; }
  [ "$(grep -o 'NET overtakes [0-9]*, MC overtakes [0-9]*' "$o/.stderr")" = "NET overtakes 0, MC overtakes 0" ] || { echo "hazard"; return; }
  grep -q "\[EVENT-PATH\] tiling ok=[0-9]* fail=0 " "$o/.stderr" || { echo "tiling"; return; }
  echo ok
}

printf "%-26s %-6s %8s %8s %8s %8s %8s  %s\n" batch table repl wb clean memw fills verdict
for sys in 1B 2B M; do for P in MESI MSI; do for V in "" _v2; do
  case $sys in
    1B) c=MultiCoreSystem_TileLink;   L=llc_controller;      size=4096 ;;
    2B) c=MultiCoreSystem_TileLink2B; L="llc_controller[*]"; size=2048 ;;
    M)  c=MultiCoreSystem_TileLinkM;  L=llc_controller;      size=4096 ;;
  esac
  X=(-p "$L.m_data_handler.cache_size(i)=$size" -p "$L.m_data_handler.pwb_size(i)=1" -p "$L.fsm_filename(s)=TLC_${P}_LLC$V")
  d="$OUT/$sys-$P${V:-_v1}"
  for t in $EEMBC; do
    ( o="$d/$t"; mkdir -p "$o"
      OCTOPUS_EVENT_DEBUG=1 OCTOPUS_HANG_DUMP=200000 OCTOPUS_MEM_HAZARD=1 timeout 600 "$BIN" -s MultiCoreSystem_Mesh -c $c "${Q[@]}" "${X[@]}" \
        -p "workload_path(s)=$ROOT/BMs/eembc-traces/$t/" -o "$o" > "$o/.stdout" 2> "$o/.stderr"; echo $? > "$o/.rc" ) &
  done; wait
  R=0 W=0 C=0 MW=0 F=0 bad=""
  for t in $EEMBC; do
    o="$d/$t"; v=$(verdict "$o" "$t")
    read -r r w f <<< "$(grep -h '^TLC_LLC [0-9]*: acquires' "$o/.stderr" | tr -d ',' | awk '{r+=$14; w+=$16; f+=$12} END{print r+0, w+0, f+0}')"
    cl=$(grep -h 'clean evictions not written back' "$o/.stderr" | awk '{s+=$3} END{print s+0}')
    mw=$(grep -o 'writes [0-9]*' "$o/.stderr" | head -1 | cut -d' ' -f2)
    if [ -z "$V" ]; then { [ "$r" = "$w" ] && [ "$w" = "$mw" ] && [ "$cl" = 0 ]; } || v="$v; accounting r $r w $w mw $mw clean $cl"
    else { [ "$r" = "$((w+cl))" ] && [ "$w" = "$mw" ]; } || v="$v; accounting r $r w $w mw $mw clean $cl"; fi
    R=$((R+r)); W=$((W+w)); C=$((C+cl)); MW=$((MW+${mw:-0})); F=$((F+f))
    [ "$v" = ok ] || bad="$bad $t($v)"
  done
  printf "%-26s %-6s %8d %8d %8d %8d %8d  %s\n" "$sys-$P" "${V:-v1}" $R $W $C $MW $F "${bad:-10/10 ok}"
done; done; done
