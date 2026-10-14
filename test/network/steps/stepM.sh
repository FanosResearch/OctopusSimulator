# Memory on the network (docs/NoC_Test_Plan.md §12a, spec §25; plan docs/NoC_Memory_Plan.md).
# Uses completed() and tiling() from step5.sh (sourced before this file).
MEMNET="-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2BM"
# MEM-OFF: the presets whose memory stays on the bus are byte-identical to their references
mem_off_refs() {   # called by make_refs
  local p t name args
  for p in "Grid2B|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B" "Grid|-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid" "Dir|-s MultiCoreSystem -c MultiCoreSystem_Directory"; do
    name=${p%%|*}; args=${p#*|}; mkdir -p "$REF/mem-off/$name"
    for t in $EEMBC; do sim "$OUT/mem-off/$name/$t" $args -p "workload_path(s)=$(W $t)/" & done; wait
    for t in $EEMBC; do sumdir "$OUT/mem-off/$name/$t" > "$REF/mem-off/$name/$t.md5"; done
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
