# Step 5 -- system wiring (docs/NoC_Test_Plan.md §8): directory MSI over the 2x2 grid Network.
GRID="-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid"
# completed as the sweeps judge it: every core's LatencyReport has at least as many rows as its
# trace has references and carries the "Average Latency" footer
completed() { # completed <outdir> <workload dir>
  local c; for c in 0 1 2 3; do
    local refs rows fin
    refs=$(wc -l < "$2/trace_C$c.trc.shared"); rows=$(awk 'END{print NR}' "$1/LatencyReport_C$c.csv" 2>/dev/null); rows=${rows:-0}
    fin=$(grep -c "Average Latency" "$1/LatencyReport_C$c.csv" 2>/dev/null); fin=${fin:-0}
    { [ "$fin" -ge 1 ] && [ "$rows" -ge "$refs" ]; } || return 1
  done; return 0
}
tiling() { grep -o "\[EVENT-PATH\].*" "$1/.stderr" | tail -1; }   # needs OCTOPUS_EVENT_DEBUG=1

step5() {
  local O="$OUT/s5" t0 m e; mkdir -p "$O"
  export OCTOPUS_EVENT_DEBUG=1
  # ---------------- S5-EEMBC-2x2, S5-TILING, S5-SPEED (parallel, each timed)
  t0=$(now)
  local t; for t in $EEMBC; do rm -rf "${O:?}/eembc/${t:?}"; mkdir -p "$O/eembc/$t"; done
  for t in $EEMBC; do
    ( s=$(now); OCTOPUS_HANG_DUMP=200000 timeout 1800 "$BIN" $GRID -p "workload_path(s)=$(W $t)/" -o "$(wpath "$O/eembc/$t")" > "$O/eembc/$t/.stdout" 2> "$O/eembc/$t/.stderr"; echo "$? $(elapsed "$s")" > "$O/eembc/$t/.rc" ) &
  done
  wait
  local ok=1 detail="" tile_ok=1 tile_detail="" speed=""
  for t in $EEMBC; do
    local rc; rc=$(cut -d' ' -f1 "$O/eembc/$t/.rc" 2>/dev/null); local secs; secs=$(cut -d' ' -f2 "$O/eembc/$t/.rc" 2>/dev/null)
    speed="$speed $t=${secs}s"
    if [ "$rc" != 0 ] || ! completed "$O/eembc/$t" "$ROOT/BMs/eembc-traces/$t"; then ok=0; detail="$detail $t(rc=$rc)"; fi
    local tl; tl=$(tiling "$O/eembc/$t")
    echo "$tl" | grep -q " fail=0 noresp=0" || { tile_ok=0; tile_detail="$tile_detail $t:${tl:-no-line}"; }
  done
  if [ $ok = 1 ]; then pass S5-EEMBC-2x2 "10/10 complete" "complete" "$(elapsed "$t0")"; else fail S5-EEMBC-2x2 "$detail" "10/10 complete" "$(elapsed "$t0")"; fi
  if [ $tile_ok = 1 ]; then pass S5-TILING "$(tiling "$O/eembc/a2time01-trace")" "fail=0 noresp=0"; else fail S5-TILING "$tile_detail" "fail=0 noresp=0"; fi
  # legacy mesh a2time01, sequential, for the speed comparison
  t0=$(now); sim "$O/legacy-a2time01" -s MultiCoreSystem_Mesh -p "workload_path(s)=$(W a2time01-trace)/"; local legacy; legacy=$(elapsed "$t0")
  t0=$(now); sim "$O/grid-a2time01" $GRID -p "workload_path(s)=$(W a2time01-trace)/"; local grid; grid=$(elapsed "$t0")
  if $PY -c "import sys; sys.exit(0 if $grid <= 3*$legacy else 1)"; then pass S5-SPEED "grid ${grid}s vs legacy ${legacy}s (parallel batch:$speed)" "<= 3x legacy"; else fail S5-SPEED "grid ${grid}s vs legacy ${legacy}s" "<= 3x legacy"; fi
  # ---------------- S5-DETERMINISM: two traces twice
  t0=$(now); ok=1
  for t in a2time01-trace mi_pingpong; do
    sim "$O/again-$t" $GRID -p "workload_path(s)=$(W $t)/"
    [ "$(sumdir "$O/eembc/$t")" = "$(sumdir "$O/again-$t")" ] || ok=0
  done
  if [ $ok = 1 ]; then pass S5-DETERMINISM "2 traces byte-identical" "identical" "$(elapsed "$t0")"; else fail S5-DETERMINISM "differs" "identical" "$(elapsed "$t0")"; fi
  # ---------------- S5-PLACEMENT: one request from core 0, LLC on router 3 then router 0 (spec s21)
  local WL="$O/one-request"; rm -rf "$WL"; mkdir -p "$WL"
  # core 0: the first reference of a2time01; cores 1-3: the same reference to other lines, issued
  # thousands of cycles later (an empty trace never terminates the CPU, so the run would not end)
  head -1 "$ROOT/BMs/eembc-traces/a2time01-trace/trace_C0.trc.shared" > "$WL/trace_C0.trc.shared"
  for c in 1 2 3; do head -1 "$ROOT/BMs/eembc-traces/a2time01-trace/trace_C0.trc.shared" | awk -v c=$c '{ $1 = sprintf("%x", strtonum("0x" $1) + 4096 * c); $4 = $4 + 5000 * c; print }' > "$WL/trace_C$c.trc.shared"; done
  sim "$O/place-r3" $GRID -p "workload_path(s)=$(wpath "$WL")/"
  sim "$O/place-r0" $GRID -p "workload_path(s)=$(wpath "$WL")/" -p "interconnect.node_map[10](i)=0"
  col() { sed -n 2p "$1/LatencyReport_C0.csv" | cut -d, -f"$2"; }     # 6 = Requst Bus, 9 = Response Bus
  m="$(col "$O/place-r3" 6)/$(col "$O/place-r3" 9)"; e="$(col "$O/place-r0" 6)/$(col "$O/place-r0" 9)"
  if [ "$m" = "8/12" ] && [ "$e" = "2/6" ]; then pass S5-PLACEMENT "router 3: $m, router 0: $e" "8/12, 2/6"; else fail S5-PLACEMENT "router 3: $m, router 0: $e" "8/12, 2/6"; fi
  # ---------------- S5-XPLAT and S5-ASAN: a2time01 on the grid under the WSL sanitizer build
  if [ -f "$ROOT/build_asan/Octopus_Simulator" ] && command -v wsl.exe > /dev/null 2>&1; then
    t0=$(now); local lr; lr="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#')"
    rm -rf "$O/wsl-a2time01"; mkdir -p "$O/wsl-a2time01"
    # detect_leaks=0: the legacy controllers leak at exit (pre-existing, outside the network); the
    # sanitizer check here is for memory errors in the run itself
    wsl.exe -e bash -lc "cd '$lr' && OCTOPUS_EVENT_DEBUG=1 ASAN_OPTIONS=detect_leaks=0 ./build_asan/Octopus_Simulator -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid -p 'workload_path(s)=BMs/eembc-traces/a2time01-trace/' -o test/network/out/s5/wsl-a2time01 > test/network/out/s5/wsl-a2time01/.stdout 2> test/network/out/s5/wsl-a2time01/.stderr"; local wrc=$?
    if [ $wrc = 0 ] && [ "$(sumdir "$O/eembc/a2time01-trace")" = "$(sumdir "$O/wsl-a2time01")" ]; then pass S5-XPLAT "identical checksums" "identical" "$(elapsed "$t0")"; else fail S5-XPLAT "rc=$wrc $(sumdir "$O/wsl-a2time01")" "$(sumdir "$O/eembc/a2time01-trace")" "$(elapsed "$t0")"; fi
    if [ $wrc = 0 ] && ! grep -q -e "ERROR: AddressSanitizer" -e "runtime error" "$O/wsl-a2time01/.stderr"; then pass S5-ASAN "no sanitizer report" "none"; else fail S5-ASAN "see $O/wsl-a2time01/.stderr" "none"; fi
  else xfail S5-XPLAT "no WSL sanitizer build"; xfail S5-ASAN "no WSL sanitizer build"; fi
  # ---------------- S5-HANGDUMP: a 4-ring under Table routing with the check disabled, every node
  # sending 5-flit packets two hops clockwise with vc_depth 2 -> wormhole deadlock -> watchdog dump
  RING="-s NetworkOnly -p interconnect.topology(s)=ring -p interconnect.dims(vi)=4 -p interconnect.component_ids(vi)=0,1,2,3 -p interconnect.routing(s)=Table -p interconnect.vc_depth(i)=2
        -p generator[*].pattern(s)=hotspot -p generator[*].packets(i)=-1 -p generator[*].rate(i)=1000 -p generator[*].payload_bytes(i)=64 -p generator[*].cycles(i)=100000
        -p generator[0].hotspot_id(i)=2 -p generator[1].hotspot_id(i)=3 -p generator[2].hotspot_id(i)=0 -p generator[3].hotspot_id(i)=1"
  rm -rf "$O/hang"; mkdir -p "$O/hang"
  ( export OCTOPUS_NET_DEBUG=nocheck OCTOPUS_HANG_DUMP=2000; timeout 300 "$BIN" $RING -o "$(wpath "$O/hang")" > "$O/hang/.stdout" 2> "$O/hang/.stderr" ); local hrc=$?
  local held; held=$(grep -c "credits 0" "$O/hang/.stderr")
  if [ $hrc = 2 ] && grep -q "wedged" "$O/hang/.stderr" && [ "$held" -ge 4 ]; then pass S5-HANGDUMP "exit 2, $held held VCs with zero credits" "ring of held VCs, exit 2"; else fail S5-HANGDUMP "rc=$hrc held=$held $(grep -m1 Network: "$O/hang/.stderr")" "wedged, >=4 held VCs, exit 2"; fi
  rm -rf "$O/hang-pos"; mkdir -p "$O/hang-pos"
  ( export OCTOPUS_HANG_DUMP=2000; timeout 300 "$BIN" $RING -o "$(wpath "$O/hang-pos")" > "$O/hang-pos/.stdout" 2> "$O/hang-pos/.stderr" ); hrc=$?
  if [ $hrc = 2 ] && grep -q "not deadlock-free" "$O/hang-pos/.stderr"; then pass S5-HANGDUMP-POS "refused at construction" "refused"; else fail S5-HANGDUMP-POS "rc=$hrc $(head -c 100 "$O/hang-pos/.stderr")" "refused at construction"; fi
  # ---------------- S5-RX-STRESS (P2-THREE-CLASS): three classes, shallow RX queues, all ten traces,
  # under FCFS and under round-robin arbitration (which reorders the classes in the fabric)
  t0=$(now); ok=1; detail=""
  local arb; for arb in FCFSArbiter RRArbiter; do
    for t in $EEMBC; do rm -rf "${O:?}/rx-$arb/${t:?}"; mkdir -p "$O/rx-$arb/$t"; done
    for t in $EEMBC; do
      ( OCTOPUS_HANG_DUMP=200000 timeout 1800 "$BIN" $GRID -p "workload_path(s)=$(W $t)/" -p "interconnect.eject_queue_depth(i)=2" -p "interconnect.arbiter_type(s)=$arb" -o "$(wpath "$O/rx-$arb/$t")" > "$O/rx-$arb/$t/.stdout" 2> "$O/rx-$arb/$t/.stderr"; echo $? > "$O/rx-$arb/$t/.rc" ) &
    done; wait
    for t in $EEMBC; do [ "$(cat "$O/rx-$arb/$t/.rc")" = 0 ] && completed "$O/rx-$arb/$t" "$ROOT/BMs/eembc-traces/$t" || { ok=0; detail="$detail $arb:$t"; }; done
  done
  if [ $ok = 1 ]; then pass S5-RX-STRESS "three classes, eject_queue_depth 2, FCFS and RR: 20/20 complete" "complete" "$(elapsed "$t0")"; else fail S5-RX-STRESS "$detail" "20/20 complete" "$(elapsed "$t0")"; fi
  unset OCTOPUS_EVENT_DEBUG
}
