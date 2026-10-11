# Step 7 -- observability (docs/NoC_Test_Plan.md §10): router trace records, the NoC report columns, the
# visualizer's router lanes.
step7() {
  local O="$OUT/s7" t0 m e; mkdir -p "$O"
  # ---- a2time01 on the grid with the raw trace on
  t0=$(now); local run="$O/a2time01"; rm -rf "${run:?}"; mkdir -p "$run"
  OCTOPUS_TRACE="$(wpath "$run/trace.bin")" OCTOPUS_HANG_DUMP=200000 timeout 1800 "$BIN" -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid -p "workload_path(s)=$(W a2time01-trace)/" -o "$(wpath "$run")" > "$run/.stdout" 2> "$run/.stderr"
  local rc=$?
  if [ $rc != 0 ] || [ ! -f "$run/trace.bin" ]; then fail S7-TRACE-HOPS "run rc=$rc" "trace written" "$(elapsed "$t0")"; fail S7-TRACKER-SUM "no run" ""; fail S7-TIMEBASE "no run" ""; fail S7-CONVERT "no run" ""
  else
    local w; for w in hops sum timebase; do
      local id; case $w in hops) id=S7-TRACE-HOPS;; sum) id=S7-TRACKER-SUM;; timebase) id=S7-TIMEBASE;; esac
      local out; out=$($PY "$HERE/s7check.py" "$run" $w 2>&1 | tail -1)
      if [ "${out#* }" = 0 ]; then pass $id "$out checked/mismatches" "0 mismatches" "$(elapsed "$t0")"; else fail $id "$out" "0 mismatches" "$(elapsed "$t0")"; fi
    done
    # ---- the converter: zero mismatches against the Logger rows, one lane per router
    t0=$(now)
    if $PY -c "import duckdb, numpy" 2>/dev/null; then
      ( cd "$ROOT" && KEEP_TRACE=1 PY=$PY bash octoviz.sh convert "$run" > "$run/.convert" 2>&1 ); local crc=$?
      local mm; mm=$(grep -o "mismatches=[0-9]*" "$run/.convert" | grep -vc "mismatches=0")
      local lanes; lanes=$($PY "$HERE/s7lanes.py" "$(wpath "$run")" 2>&1 | tail -1)
      if [ $crc = 0 ] && [ "$mm" = 0 ] && [ "$lanes" = 4 ]; then pass S7-CONVERT "all checks mismatches=0, $lanes router lanes" "0 mismatches, 4 lanes" "$(elapsed "$t0")"
      else fail S7-CONVERT "rc=$crc nonzero-mismatch lines=$mm lanes=$lanes (see $run/.convert)" "0 mismatches, 4 lanes" "$(elapsed "$t0")"; fi
    else xfail S7-CONVERT "python lacks duckdb/numpy (pip install duckdb numpy)"; fi
  fi
  # ---- S7-AHEAD: the contention case of spec s21 on the 2x2 grid: core 1 streams a 9-flit write-back
  # 1->3 from cycle 1; core 0's 1-flit request 0->3 (injected at cycle 3) arrives at router 1 while the
  # write-back's body flits hold the port to 3 and loses four switch elections to owner 1; alone it loses none
  local G22="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=2,2 -p interconnect.component_ids(vi)=0,1,2,3 -p generator[*].pattern(s)=none"
  local REQ="-p generator[0].pattern(s)=hotspot -p generator[0].hotspot_id(i)=3 -p generator[0].packets(i)=1 -p generator[0].rate(i)=1000 -p generator[0].payload_bytes(i)=0 -p generator[0].cls(i)=0 -p generator[0].start(i)=3"
  local WB="-p generator[1].pattern(s)=hotspot -p generator[1].hotspot_id(i)=3 -p generator[1].packets(i)=1 -p generator[1].rate(i)=1000 -p generator[1].payload_bytes(i)=128 -p generator[1].cls(i)=2"
  rm -rf "${O:?}/ahead" "${O:?}/alone"; mkdir -p "$O/ahead" "$O/alone"
  "$BIN" -s NetworkOnly $G22 $REQ $WB -o "$(wpath "$O/ahead")" > "$O/ahead/.stdout" 2> "$O/ahead/.stderr"
  "$BIN" -s NetworkOnly $G22 $REQ -o "$(wpath "$O/alone")" > "$O/alone/.stdout" 2> "$O/alone/.stderr"
  m=$(grep "^ahead,3" "$O/ahead/NetworkStats.csv" | cut -d, -f5,6); e=$(grep "^ahead,3" "$O/alone/NetworkStats.csv" | cut -d, -f5,6)
  local wait; wait=$($PY "$HERE/netstat.py" "$O/ahead/NetworkStats.csv" class 0 4)
  if [ "$m" = "4,1" ] && [ "$e" = "0,-1" ] && [ "$wait" = 4 ]; then pass S7-AHEAD "ahead 4 to owner 1, wait 4; alone 0" "4,1 / 0"; else fail S7-AHEAD "contended $m wait $wait, alone $e" "4,1 wait 4 / 0,-1"; fi
  # ---- S7-VIEWER-API: the viewer's server lists a lane per router and per LLC bank, the contention grid
  # and the router intervals, on a converted two-bank grid run
  t0=$(now); local vr="$O/viewer"; rm -rf "${vr:?}"; mkdir -p "$vr/grid2b"
  OCTOPUS_TRACE="$(wpath "$vr/grid2b/trace.bin")" timeout 1800 "$BIN" -s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid2B -p "workload_path(s)=$(W a2time01-trace)/" -o "$(wpath "$vr/grid2b")" > "$vr/grid2b/.stdout" 2> "$vr/grid2b/.stderr"
  if $PY -c "import duckdb, numpy" 2>/dev/null && ( cd "$ROOT" && PY=$PY bash octoviz.sh convert "$vr/grid2b" > "$vr/.convert" 2>&1 ); then
    local va; va=$($PY "$HERE/s7viewer.py" "$vr" 4 10,11 2>&1 | tail -1)
    case "$va" in ok*) pass S7-VIEWER-API "${va#ok }" "4 router lanes, banks 10 and 11, heat and intervals" "$(elapsed "$t0")";; *) fail S7-VIEWER-API "$va" "4 router lanes, banks 10 and 11" "$(elapsed "$t0")";; esac
  else xfail S7-VIEWER-API "python lacks duckdb/numpy or the conversion failed (see $vr/.convert)"; fi
  xfail S7-VIEWER "manual: the router lanes, the route of a selected request and the contention grid are checked by eye once per release"
}
