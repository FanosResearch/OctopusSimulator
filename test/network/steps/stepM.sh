# Memory on the network (docs/NoC_Test_Plan.md §12a, spec §25; plan docs/NoC_Memory_Plan.md).
# Uses completed() and tiling() from step5.sh (sourced before this file).
MEMNET="-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2BM"
# MEM-OFF: the presets whose memory stays on the bus are byte-identical to their references
mem_off_refs() {   # called by make_refs
  local p t name args
  for p in "Grid2B|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B" "Grid|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid" "Dir|-s MultiCoreSystem -c MultiCoreSystem_Directory"; do
    name=${p%%|*}; args=${p#*|}; mkdir -p "$REF/mem-off/$name"
    for t in $EEMBC; do sim "$OUT/mem-off/$name/$t" $args -p "workload_path(s)=$(W $t)/" & done; wait
    for t in $EEMBC; do write_ref "$OUT/mem-off/$name/$t" "$REF/mem-off/$name/$t.md5"; done
    echo "mem-off $name: $(cat "$REF/mem-off/$name/"*.md5 | md5sum | cut -c1-12)"
  done
}

# mem_run <outdir> <extra args...>: one Grid2BM run with the hazard detector and the hang watchdog
mem_run() {
  local o="$1"; shift; rm -rf "$o"; mkdir -p "$o"
  ( OCTOPUS_HANG_DUMP=200000 OCTOPUS_MEM_HAZARD=1 timeout 1800 "$BIN" $MEMNET "$@" -o "$(wpath "$o")" > "$o/.stdout" 2> "$o/.stderr"; echo $? > "$o/.rc" )
}
mem_hazard_ok() { [ "$(grep -o 'NET overtakes [0-9]*, MC overtakes [0-9]*' "$1/.stderr")" = "NET overtakes 0, MC overtakes 0" ]; }
mem_refused() { grep "^endpoint" "$1/NetworkStats.csv" 2>/dev/null | cut -d, -f7 | awk '{s+=$1} END{print s+0}'; }

stepM2() {
  local O="$OUT/mem" t0 t ok detail; mkdir -p "$O"
  # ---------------- MEM-OFF: memory on the bus is byte-identical to the references
  t0=$(now); ok=1; detail=""
  local p name args
  for p in "Grid2B|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B" "Grid|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid" "Dir|-s MultiCoreSystem -c MultiCoreSystem_Directory"; do
    name=${p%%|*}; args=${p#*|}
    for t in $EEMBC; do sim "$O/off/$name/$t" $args -p "workload_path(s)=$(W $t)/" & done; wait
    for t in $EEMBC; do
      [ "$(sumdir "$O/off/$name/$t")" = "$(cat "$REF/mem-off/$name/$t.md5" 2>/dev/null || echo missing)" ] || { ok=0; detail="$detail $name:$t"; }
    done
  done
  if [ $ok = 1 ]; then pass MEM-OFF "30/30 identical" "identical" "$(elapsed "$t0")"; else fail MEM-OFF "differs:$detail" "mem-off references" "$(elapsed "$t0")"; fi

  # ---------------- MEM-EEMBC and MEM-HAZARD (FCFS and RR), MEM-ADMISSION (inject 4, eject 2)
  local arb mode
  for arb in FCFSArbiter RRArbiter; do for mode in plain stress; do
    local X=""; [ $mode = stress ] && X="-p interconnect.inject_queue_depth(i)=4 -p interconnect.eject_queue_depth(i)=2"
    for t in $EEMBC; do mem_run "$O/$mode-$arb/$t" -p "workload_path(s)=$(W $t)/" -p "interconnect.arbiter_type(s)=$arb" $X & done
  done; done; wait
  # MEM-EEMBC: FCFS plain, complete, DRAM stage present
  ok=1; detail=""
  for t in $EEMBC; do
    local d="$O/plain-FCFSArbiter/$t"
    { [ "$(cat "$d/.rc")" = 0 ] && completed "$d" "$ROOT/BMs/eembc-traces/$t"; } || { ok=0; detail="$detail $t(rc=$(cat "$d/.rc"))"; }
    [ "$t" = mi_pingpong ] || [ "$(awk -F, 'NR>1 && $8+0>m{m=$8+0} END{print m+0}' "$d/Summary.csv")" -gt 0 ] || { ok=0; detail="$detail $t(no DRAM stage)"; }
  done
  if [ $ok = 1 ]; then pass MEM-EEMBC "10/10 complete, DRAM stage present" "complete"; else fail MEM-EEMBC "$detail" "10/10 complete, DRAM stage present"; fi
  # MEM-HAZARD: plain runs, both arbiters
  ok=1; detail=""
  for arb in FCFSArbiter RRArbiter; do for t in $EEMBC; do
    local d="$O/plain-$arb/$t"
    { [ "$(cat "$d/.rc")" = 0 ] && completed "$d" "$ROOT/BMs/eembc-traces/$t" && mem_hazard_ok "$d" && ! grep -q Fault "$d/.stdout"; } || { ok=0; detail="$detail $arb:$t"; }
  done; done
  if [ $ok = 1 ]; then pass MEM-HAZARD "20/20 complete, 0 overtakes" "0 overtakes"; else fail MEM-HAZARD "$detail" "20/20 complete, 0 overtakes, no fault"; fi
  # MEM-ADMISSION: stress runs, both arbiters: complete, no refused push, memory controller never exits
  ok=1; detail=""
  for arb in FCFSArbiter RRArbiter; do for t in $EEMBC; do
    local d="$O/stress-$arb/$t" r; r=$(mem_refused "$d")
    { [ "$(cat "$d/.rc")" = 0 ] && completed "$d" "$ROOT/BMs/eembc-traces/$t" && [ "$r" = 0 ] && mem_hazard_ok "$d" && ! grep -q Fault "$d/.stdout"; } || { ok=0; detail="$detail $arb:$t(rc=$(cat "$d/.rc"),refused=$r)"; }
  done; done
  if [ $ok = 1 ]; then pass MEM-ADMISSION "20/20 complete with inject 4 / eject 2, 0 refused" "complete, 0 refused"; else fail MEM-ADMISSION "$detail" "20/20 complete, 0 refused"; fi

  # ---------------- MEM-DETERMINISM: a second FCFS run of two traces is byte-identical
  ok=1; detail=""
  for t in a2time01-trace cacheb01-trace; do
    mem_run "$O/det/$t" -p "workload_path(s)=$(W $t)/" -p "interconnect.arbiter_type(s)=FCFSArbiter"
    [ "$(sumdir "$O/det/$t")" = "$(sumdir "$O/plain-FCFSArbiter/$t")" ] || { ok=0; detail="$detail $t"; }
  done
  if [ $ok = 1 ]; then pass MEM-DETERMINISM "identical" "identical"; else fail MEM-DETERMINISM "differs:$detail" "identical"; fi
}

stepM3() {
  local O="$OUT/mem3" t0; mkdir -p "$O"
  # ---------------- MEM-CLASS: memory packets travel in the classes of spec s25.3 (per-endpoint class counts)
  t0=$(now)
  mem_run "$O/cls" -p "workload_path(s)=$(W a2time01-trace)/"
  local st="$O/cls/NetworkStats.csv" reads writes
  reads=$(grep -o 'reads [0-9]*' "$O/cls/.stderr" | cut -d' ' -f2); writes=$(grep -o 'writes [0-9]*' "$O/cls/.stderr" | cut -d' ' -f2)
  epc() { awk -F, -v e="$1" -v c="$2" '$1=="epclass" && $2==e && $3==c {print $5}' "$st"; }
  local m0 m1 m2 p0 p1 p2 q0 q1 q2
  m0=$(epc 100 0); m1=$(epc 100 1); m2=$(epc 100 2)
  p0=$(epc 110 0)$(epc 111 0); p1=$(epc 110 1)$(epc 111 1); p2=$(( $(epc 110 2) + $(epc 111 2) ))
  if [ -z "$m0" ] && [ "$m1" = "$reads" ] && [ "$m2" = "$writes" ] && [ -z "$p0$p1" ] && [ "$p2" = $((reads + writes)) ]; then
    pass MEM-CLASS "memory: forward $m1 = reads, response $m2 = writes; ports: response $p2 = fills + acks" "reads FORWARD, writes/fills/acks RESPONSE" "$(elapsed "$t0")"
  else fail MEM-CLASS "memory req=${m0:-0} fwd=$m1 resp=$m2 (reads $reads writes $writes); ports req/fwd=${p0}${p1} resp=$p2" "reads FORWARD, writes/fills/acks RESPONSE" "$(elapsed "$t0")"; fi

  # ---------------- MEM-CHECKER: the memory controllers are checked; a memory read in REQUEST is refused
  t0=$(now)
  local v; v=$(grep 'Protocol-network check' "$O/cls/.stdout")
  local vb; sim "$O/bus" -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B -p "workload_path(s)=$(W empty-trace)/" > /dev/null 2>&1
  vb=$(grep 'Protocol-network check' "$O/bus/.stdout")
  local cm cb; cm=$(echo "$v" | grep -o '[0-9]* cells' | cut -d' ' -f1); cb=$(echo "$vb" | grep -o '[0-9]* cells' | cut -d' ' -f1)
  rm -rf "$O/neg"; mkdir -p "$O/neg"
  ( OCTOPUS_NET_DEBUG=memread-request timeout 300 "$BIN" $MEMNET -p "workload_path(s)=$(W a2time01-trace)/" -o "$(wpath "$O/neg")" > "$O/neg/.stdout" 2> "$O/neg/.stderr" ); local nrc=$?
  if echo "$v" | grep -q "consumption order holds" && [ -n "$cm" ] && [ "$cm" = $((cb + 2)) ] && [ $nrc = 2 ] && grep -q "emits GetData in REQUEST" "$O/neg/.stderr"; then
    pass MEM-CHECKER "holds with $cm cells (bus memory: $cb, + 2 memory-controller cells); read in REQUEST refused (exit 2)" "holds; negative refused" "$(elapsed "$t0")"
  else fail MEM-CHECKER "verdict '$v' cells $cm/$cb negative rc=$nrc" "holds; negative refused with exit 2" "$(elapsed "$t0")"; fi
}

# unloaded latency of a packet of f flits through R routers (spec s13.2): R*rl + (R-1)*L + (f-1); Network.csv: rl 2, L 1
mem_unl() { echo $(( $1 * 2 + ($1 - 1) * 1 + $2 - 1 )); }

stepM4() {
  local O="$OUT/mem4" t0 ok detail; mkdir -p "$O"
  if ! py_has numpy; then
    xfail MEM-UNLOADED "$(py_lacks numpy)"; xfail MEM-COLUMNS "$(py_lacks numpy)"; xfail MEM-VIEWER "$(py_lacks numpy duckdb)"
  else
    # ---------------- MEM-UNLOADED: the fastest read (1 flit) and fill (5 flits) per bank equal the formula, two placements
    t0=$(now); ok=1; detail=""
    local mc
    for mc in 1 3; do
      local run="$O/unl$mc"; rm -rf "$run"; mkdir -p "$run"
      OCTOPUS_TRACE="$(wpath "$run/trace.bin")" timeout 1800 "$BIN" $MEMNET -p "workload_path(s)=$(W a2time01-trace)/" -p "interconnect.node_map[100](i)=$mc" -o "$(wpath "$run")" > "$run/.stdout" 2> "$run/.stderr"
      $PY "$HERE/memunloaded.py" "$run" 10 11 > "$run/.unl" 2>&1
      # routers crossed from bank 10's port (router 0) and bank 11's port (router 3) to the memory controller
      local r10 r11; if [ $mc = 1 ]; then r10=2; r11=2; else r10=3; r11=1; fi
      local want="FILL 10 $(mem_unl $r10 5)|FILL 11 $(mem_unl $r11 5)|MEM_READ 10 $(mem_unl $r10 1)|MEM_READ 11 $(mem_unl $r11 1)"
      local got; got=$(cut -d' ' -f1-3 "$run/.unl" | paste -sd'|')
      [ "$got" = "$want" ] || { ok=0; detail="$detail mc@$mc: got '$got' want '$want'"; }
    done
    if [ $ok = 1 ]; then pass MEM-UNLOADED "read 5/8/2, fill 9/12/6 as R*rl+(R-1)L+(f-1)" "formula" "$(elapsed "$t0")"; else fail MEM-UNLOADED "$detail" "formula"; fi
    # ---------------- MEM-COLUMNS: NoC Mem Hops = the trace's memory hops per request; coherence trackers unchanged; absent with memory on the bus
    t0=$(now)
    local mcol; mcol=$($PY "$HERE/memcolumns.py" "$O/unl1" 2>&1 | tail -1)
    local tsum; tsum=$($PY "$HERE/s7check.py" "$O/unl1" sum 2>&1 | tail -1)
    local busrun="$O/bus"; sim "$busrun" -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B -p "workload_path(s)=$(W empty-trace)/"
    if [ "${mcol#* }" = 0 ] && [ "${tsum#* }" = 0 ] && ! head -1 "$busrun/LatencyReport_C0.csv" | grep -q "NoC Mem"; then
      pass MEM-COLUMNS "Mem Hops $mcol rows/mismatches; coherence trackers $tsum; absent on the bus" "0 mismatches" "$(elapsed "$t0")"
    else fail MEM-COLUMNS "memcolumns '$mcol' trackers '$tsum'" "0 mismatches, absent on the bus" "$(elapsed "$t0")"; fi
    # ---------------- MEM-VIEWER: the converter's checks are clean and draws one MEM_BUS interval per memory packet
    t0=$(now)
    if py_has duckdb; then
      ( cd "$ROOT" && KEEP_TRACE=1 PY=$PY bash octoviz.sh convert "$O/unl1" > "$O/unl1/.convert" 2>&1 ); local crc=$?
      local mm; mm=$(grep -o "mismatches=[0-9]*" "$O/unl1/.convert" | grep -vc "mismatches=0")
      local memiv; memiv=$(awk '$1=="MEM_BUS" {print $2}' "$O/unl1/.convert" | head -1)
      local st="$O/unl1/NetworkStats.csv" pk
      pk=$(awk -F, '$1=="received" && ($2==100 || $2==110 || $2==111) {s+=$5} END{print s+0}' "$st")
      if [ $crc = 0 ] && [ "$mm" = 0 ] && ! grep -q "ends without a start" "$O/unl1/.convert" && [ "$memiv" = "$pk" ]; then
        pass MEM-VIEWER "converted, 0 mismatches, $memiv MEM_BUS intervals = memory packets" "clean" "$(elapsed "$t0")"
      else fail MEM-VIEWER "rc=$crc nonzero-mismatch lines=$mm MEM_BUS intervals=$memiv packets=$pk" "clean, one interval per packet" "$(elapsed "$t0")"; fi
    else xfail MEM-VIEWER "$(py_lacks duckdb)"; fi
  fi
  # ---------------- MEM-TILING: the event path tiles on every row of the ten traces
  t0=$(now); ok=1; detail=""
  local t; for t in $EEMBC; do
    ( rm -rf "$O/tile/$t"; mkdir -p "$O/tile/$t"; OCTOPUS_EVENT_DEBUG=1 timeout 1800 "$BIN" $MEMNET -p "workload_path(s)=$(W $t)/" -o "$(wpath "$O/tile/$t")" > "$O/tile/$t/.stdout" 2> "$O/tile/$t/.stderr" ) &
  done; wait
  for t in $EEMBC; do
    local tl; tl=$(tiling "$O/tile/$t")
    echo "$tl" | grep -q "fail=0 " || { ok=0; detail="$detail $t:'$tl'"; }
    [ "$t" = mi_pingpong ] || [ "$(awk -F, 'NR>1 && $7+0>m{m=$7+0} END{print m+0}' "$O/tile/$t/Summary.csv")" -gt 0 ] || { ok=0; detail="$detail $t(no L2-DRAM bus stage)"; }
  done
  if [ $ok = 1 ]; then pass MEM-TILING "10/10 tile, L2-DRAM bus stage present" "fail=0" "$(elapsed "$t0")"; else fail MEM-TILING "$detail" "fail=0 on every trace"; fi
}

stepM5() {
  local O="$OUT/mem5" t0 ok detail; mkdir -p "$O"
  local MEM2="-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B2M"
  # ---------------- MEM-MULTI: two memory controllers, 4 KiB pages interleaved (spec s25.4)
  t0=$(now); ok=1; detail=""
  local arb mode t
  for arb in FCFSArbiter RRArbiter; do for mode in plain stress; do
    local X=""; [ $mode = stress ] && X="-p interconnect.inject_queue_depth(i)=4 -p interconnect.eject_queue_depth(i)=2"
    for t in $EEMBC; do
      ( o="$O/$mode-$arb/$t"; rm -rf "$o"; mkdir -p "$o"
        OCTOPUS_HANG_DUMP=200000 OCTOPUS_MEM_HAZARD=1 timeout 1800 "$BIN" $MEM2 -p "workload_path(s)=$(W $t)/" -p "interconnect.arbiter_type(s)=$arb" $X -o "$(wpath "$o")" > "$o/.stdout" 2> "$o/.stderr"; echo $? > "$o/.rc" ) &
    done
  done; done; wait
  for arb in FCFSArbiter RRArbiter; do for mode in plain stress; do for t in $EEMBC; do
    local d="$O/$mode-$arb/$t" r; r=$(mem_refused "$d")
    { [ "$(cat "$d/.rc")" = 0 ] && completed "$d" "$ROOT/BMs/eembc-traces/$t" && mem_hazard_ok "$d" && [ "$r" = 0 ] && ! grep -q Fault "$d/.stdout"; } || { ok=0; detail="$detail $mode-$arb:$t(rc=$(cat "$d/.rc"),refused=$r)"; }
  done; done; done
  if [ $ok = 1 ]; then pass MEM-MULTI "40/40 complete, 0 overtakes, 0 refused; routing: MEM-MULTI-MAP" "complete" "$(elapsed "$t0")"
  else fail MEM-MULTI "$detail" "40/40 complete" "$(elapsed "$t0")"; fi
  # the map (its own result, numpy): every memory request of a2time01 went to the controller mem_ids names
  t0=$(now)
  if py_has numpy; then
    local run="$O/map" mm n100="" n101="" first; rm -rf "$run"; mkdir -p "$run"
    OCTOPUS_TRACE="$(wpath "$run/trace.bin")" timeout 1800 "$BIN" $MEM2 -p "workload_path(s)=$(W a2time01-trace)/" -o "$(wpath "$run")" > "$run/.stdout" 2> "$run/.stderr"
    mm=$($PY "$HERE/memmulti.py" "$run" 6 100 101 2>&1)
    first=$(echo "$mm" | head -1)
    n100=$(echo "$mm" | awk '$1==100 {print $2}'); n101=$(echo "$mm" | awk '$1==101 {print $2}')
    if [ "${first#* }" = "0 0" ] && [ "${n100:-0}" -gt 0 ] 2>/dev/null && [ "${n101:-0}" -gt 0 ] 2>/dev/null; then
      pass MEM-MULTI-MAP "${first%% *} packets, 0 misrouted (100: $n100, 101: $n101)" "routed by the map, both controllers used" "$(elapsed "$t0")"
    else fail MEM-MULTI-MAP "'$first' (100: ${n100:-none}, 101: ${n101:-none})" "0 misrouted, both controllers used" "$(elapsed "$t0")"; fi
  else xfail MEM-MULTI-MAP "$(py_lacks numpy)"; fi
}

stepM6() {
  local O="$OUT/mem6" t0 ok detail; mkdir -p "$O"
  local MC="-p main_memory_type(s)=MCsim -p mcsim_scheduler(s)=FRFCFS"
  # ---------------- MEM-MCSIM: MCsim behind one and two network memory controllers (spec s25.4)
  t0=$(now); ok=1; detail=""
  local p t
  for p in Grid2BM Grid2B2M; do for t in $EEMBC; do
    ( o="$O/$p/$t"; rm -rf "$o"; mkdir -p "$o"
      OCTOPUS_HANG_DUMP=200000 OCTOPUS_MEM_HAZARD=1 OCTOPUS_EVENT_DEBUG=1 timeout 1800 "$BIN" -s MultiCoreSystem_Mesh -c MultiCoreSystem_$p $MC -p "workload_path(s)=$(W $t)/" -o "$(wpath "$o")" > "$o/.stdout" 2> "$o/.stderr"; echo $? > "$o/.rc" ) &
  done; done; wait
  for p in Grid2BM Grid2B2M; do for t in $EEMBC; do
    local d="$O/$p/$t"
    { [ "$(cat "$d/.rc")" = 0 ] && completed "$d" "$ROOT/BMs/eembc-traces/$t" && mem_hazard_ok "$d" && tiling "$d" | grep -q "fail=0 "; } || { ok=0; detail="$detail $p:$t(rc=$(cat "$d/.rc"))"; continue; }
    # conservation: every read the memory controllers received was answered with one fill
    local st="$d/NetworkStats.csv" rd fl
    rd=$(awk -F, '$1=="epclass" && ($2==100 || $2==101) && $3==1 {s+=$5} END{print s+0}' "$st")
    fl=$(grep -o 'reads [0-9]*' "$d/.stderr" | cut -d' ' -f2)
    [ "$rd" = "$fl" ] || { ok=0; detail="$detail $p:$t(reads received $rd sent $fl)"; }
  done; done
  # determinism: three runs of a2time01 on two controllers are byte-identical
  local s1; s1=$(sumdir "$O/Grid2B2M/a2time01-trace")
  local i; for i in 2 3; do
    sim "$O/det$i" -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B2M $MC -p "workload_path(s)=$(W a2time01-trace)/"
    [ "$(sumdir "$O/det$i")" = "$s1" ] || { ok=0; detail="$detail determinism:run$i"; }
  done
  if [ $ok = 1 ]; then pass MEM-MCSIM "20/20 complete with MCsim on one and two controllers, 0 overtakes, 0 tiling failures, reads conserved, deterministic" "complete, deterministic" "$(elapsed "$t0")"
  else fail MEM-MCSIM "$detail" "complete, deterministic" "$(elapsed "$t0")"; fi
}
