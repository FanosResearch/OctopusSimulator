# Directory MESI and MOESI beyond the MSI presets: the bus directory preset and the network presets (one LLC,
# LLC banks, memory on the network, two memory controllers), FCFS and RR, normal and small endpoint queues.
# Uses completed() from step5.sh and mem_hazard_ok / mem_refused from stepM.sh.
dir_proto_args() { # <MESI|MOESI> <single|banked>
  local l1p l1f llp llf L
  case $1 in
    MESI)  l1p=DIRECTORY_MESI;  l1f=MESI_directory_v2;  llp=DIRECTORY_LLC_MESI;  llf=MESI_LLC_directory_v2;;
    MOESI) l1p=DIRECTORY_MOESI; l1f=MOESI_directory_v2; llp=DIRECTORY_LLC_MOESI; llf=MOESI_LLC_directory_v2;;
  esac
  [ $2 = banked ] && L='llc_controller[*]' || L='llc_controller'
  echo "-p cache_controller[*].protocol_type(s)=$l1p -p cache_controller[*].fsm_filename(s)=$l1f -p $L.protocol_type(s)=$llp -p $L.fsm_filename(s)=$llf"
}
dir_run() { # <outdir> <args...>
  local o="$1"; shift; rm -rf "$o"; mkdir -p "$o"
  ( OCTOPUS_HANG_DUMP=200000 OCTOPUS_MEM_HAZARD=1 timeout 1800 "$BIN" "$@" -o "$(wpath "$o")" > "$o/.stdout" 2> "$o/.stderr"; echo $? > "$o/.rc" )
}
dir_ok() { # <outdir> <trace>
  [ "$(cat "$1/.rc")" = 0 ] && completed "$1" "$ROOT/BMs/eembc-traces/$2" && mem_hazard_ok "$1" && [ "$(mem_refused "$1")" = 0 ] && ! grep -q Fault "$1/.stdout"
}

stepD() {
  local O="$OUT/dir" t0 ok detail t c arb mode; mkdir -p "$O"
  # ---------------- DIR-MESI: 17 configurations x 10 traces
  t0=$(now); ok=1; detail=""
  local -a cells=("Dir|-s MultiCoreSystem -c MultiCoreSystem_Directory $(dir_proto_args MESI single)")
  for c in Grid Grid2B Grid2BM Grid2B2M; do
    local kind=banked; [ $c = Grid ] && kind=single
    for arb in FCFSArbiter RRArbiter; do for mode in plain stress; do
      local X=""; [ $mode = stress ] && X="-p interconnect.inject_queue_depth(i)=4 -p interconnect.eject_queue_depth(i)=2"
      cells+=("$c-$mode-${arb%Arbiter}|-s MultiCoreSystem_Mesh -c MultiCoreSystem_$c $(dir_proto_args MESI $kind) -p interconnect.arbiter_type(s)=$arb $X")
    done; done
  done
  local cell n=0
  for cell in "${cells[@]}"; do
    for t in $EEMBC; do dir_run "$O/mesi/${cell%%|*}/$t" ${cell#*|} -p "workload_path(s)=$(W $t)/" & n=$((n+1)); [ $((n % 20)) = 0 ] && wait; done
  done; wait
  for cell in "${cells[@]}"; do for t in $EEMBC; do dir_ok "$O/mesi/${cell%%|*}/$t" $t || { ok=0; detail="$detail ${cell%%|*}:$t"; }; done; done
  if [ $ok = 1 ]; then pass DIR-MESI "${#cells[@]} configurations x 10 traces complete, 0 faults, 0 overtakes, 0 refused" "complete" "$(elapsed "$t0")"
  else fail DIR-MESI "$detail" "all complete, no fault" "$(elapsed "$t0")"; fi
  # ---------------- DIR-MOESI-BUS: the bus directory preset (on the network MOESI is refused by the protocol check)
  t0=$(now); ok=1; detail=""
  for t in $EEMBC; do dir_run "$O/moesi/Dir/$t" -s MultiCoreSystem -c MultiCoreSystem_Directory $(dir_proto_args MOESI single) -p "workload_path(s)=$(W $t)/" & done; wait
  for t in $EEMBC; do dir_ok "$O/moesi/Dir/$t" $t || { ok=0; detail="$detail $t"; }; done
  if [ $ok = 1 ]; then pass DIR-MOESI-BUS "10/10 complete, 0 faults" "complete" "$(elapsed "$t0")"; else fail DIR-MOESI-BUS "$detail" "10/10 complete"; fi
  stepD_moesi_net
}

# DIR-MOESI-NET: directory MOESI on the network presets (Grid, Grid2B, Grid2BM, Grid2B2M; FCFS/RR; normal/small queues).
# Known open case: N_ad + PutM_Data_fromOwner (an M owner evicting while the home evicts from O) needs a design decision
# (docs/NoC_Implementation_Report.md); a run that faults there is reported as XFAIL, anything else fails.
stepD_moesi_net() {
  local O="$OUT/dir/moesi-net" t0 ok=1 detail="" known="" c arb mode t n=0; mkdir -p "$O"; t0=$(now)
  local -a cells=()
  for c in Grid Grid2B Grid2BM Grid2B2M; do
    local kind=banked; [ $c = Grid ] && kind=single
    for arb in FCFSArbiter RRArbiter; do for mode in plain stress; do
      local X=""; [ $mode = stress ] && X="-p interconnect.inject_queue_depth(i)=4 -p interconnect.eject_queue_depth(i)=2"
      cells+=("$c-$mode-${arb%Arbiter}|-s MultiCoreSystem_Mesh -c MultiCoreSystem_$c $(dir_proto_args MOESI $kind) -p interconnect.arbiter_type(s)=$arb $X")
    done; done
  done
  local cell
  for cell in "${cells[@]}"; do for t in $EEMBC; do dir_run "$O/${cell%%|*}/$t" ${cell#*|} -p "workload_path(s)=$(W $t)/" & n=$((n+1)); [ $((n % 20)) = 0 ] && wait; done; done; wait
  local total=0 good=0
  for cell in "${cells[@]}"; do for t in $EEMBC; do
    local d="$O/${cell%%|*}/$t"; total=$((total+1))
    if dir_ok "$d" $t; then good=$((good+1))
    elif grep -q 'line state 9, addr [^,]*, msg [0-9]*, source 0, owner [0-9]*, from [0-9]*, type 3, kind 4, data 1' "$d/.stdout"; then known="$known ${cell%%|*}:$t"
    else ok=0; detail="$detail ${cell%%|*}:$t"; fi
  done; done
  if [ $ok = 0 ]; then fail DIR-MOESI-NET "$detail" "$good/$total complete; only the known N_ad+PutM case may fail" "$(elapsed "$t0")"
  elif [ -n "$known" ]; then xfail DIR-MOESI-NET "$good/$total complete; known N_ad + PutM_Data_fromOwner case:$known"
  else pass DIR-MOESI-NET "$good/$total complete" "complete" "$(elapsed "$t0")"; fi
}
