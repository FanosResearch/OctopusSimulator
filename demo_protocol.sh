#!/usr/bin/env bash
#
# demo_protocol.sh -- the "add a protocol" demo (see docs/AddingAProtocol.md).
#
# MI (Modified/Invalid) is a 9-state CSV FSM added with NO C++ and NO recompile:
# it runs on the existing SNOOP_MSI handler + MSI_LLC via one FSM swap. This script
# runs the mi_pingpong read-sharing toy under MESI and MI and prints:
#   1) the cost   -- MESI shares the reads; MI takes every read exclusive (ping-pong)
#   2) the coherence trace on the shared line -- MI's I->M path vs MESI's I->S path
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; cd "$ROOT"
BIN="$ROOT/build/Octopus_Simulator"; [ -x "$BIN.exe" ] && BIN="$BIN.exe"
MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
[ -d "$MINGW" ] && export PATH="$MINGW:$ROOT/build:$PATH"

CFG="$ROOT/configuration/SystemConfigurations/MultiCoreSystem.csv"
cp "$CFG" "$CFG.demobak"; trap 'cp "$CFG.demobak" "$CFG"; rm -f "$CFG.demobak"' EXIT   # leave config untouched
cp "$ROOT/configuration/SystemConfigurations/MultiCoreSystem_Snoop.csv" "$CFG"   # MSI/snoop base
WP="$ROOT/BMs/eembc-traces/mi_pingpong"
wp_arg(){ cygpath -m "$WP" 2>/dev/null || echo "$WP"; }

MESI=( -p "cache_controller[*].protocol_type(s)=SNOOP_MESI"
       -p "cache_controller[*].fsm_filename(s)=MESI_splitBus_snooping"
       -p "llc_controller.protocol_type(s)=SNOOP_LLC_MESI"
       -p "llc_controller.fsm_filename(s)=MESI_LLC"
       -p "cache_controller_type(s)=CacheControllerExclusive" )
MI=(   -p "cache_controller[*].fsm_filename(s)=MI_splitBus_snooping" )

run(){ rm -f "$WP/newLogger"/*.csv 2>/dev/null; mkdir -p "$WP/newLogger"
       "$BIN" -s MultiCoreSystem -p "workload_path(s)=$(wp_arg)/" "$@" >/dev/null 2>&1; }
fin(){ awk -F, 'NR>1{if($12>m)m=$12}END{print m+0}' "$WP/newLogger/Summary.csv"; }

echo "== Cost of the two protocols on the SAME read-sharing workload =="
run "${MESI[@]}"; printf "   MESI  (reads SHARE)     -> finish = %s cycles\n" "$(fin)"
run "${MI[@]}";   printf "   MI    (reads PING-PONG) -> finish = %s cycles\n" "$(fin)"

echo
echo "== Coherence trace on the shared line 0x1000 (a load's transition) =="
DBG=( -p "cache_controller[*].dprint.enable(i)=1"
      -p "cache_controller[*].dprint.print_preamble(i)=1"
      -p "cache_controller[*].dprint.print_name(i)=1"
      -p "cache_controller[*].dprint.print_addr(i)=1" )
COH="$WP/coh_demo.csv"
COH_ARG="$(cygpath -m "$COH" 2>/dev/null || echo "$COH")"   # Windows path for the binary's fopen
trace1(){ grep -m1 -E ',4096,.*--Load-->' "$COH" | grep -oE '[A-Za-z0-9_]+ --Load--> [A-Za-z0-9_]+'; }
run "${MI[@]}"   -p "cache_controller[*].dprint.target(s)=$COH_ARG" "${DBG[@]}"
printf "   MI:   %s   (GetM: the read goes exclusive -> line ping-pongs)\n" "$(trace1)"
run "${MESI[@]}" -p "cache_controller[*].dprint.target(s)=$COH_ARG" "${DBG[@]}"
printf "   MESI: %s   (GetS: the read is shared -> cores share)\n" "$(trace1)"
rm -f "$COH"
echo
echo "MI = one CSV file (Protocols_FSM/MI_splitBus_snooping.csv), zero C++, zero recompiles."
