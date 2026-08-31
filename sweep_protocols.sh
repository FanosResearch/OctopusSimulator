#!/usr/bin/env bash
# sweep_protocols.sh -- run every coherence protocol (snoop + directory) across
# the EEMBC and/or SPLASH-2 suites, reporting completion status AND runtime.
#
# Each protocol config is produced by rewriting 4 lines (L1+LLC protocol_type and
# fsm_filename) on top of the family preset (MultiCoreSystem_{Snoop,Directory}.csv);
# controller types and bus stay fixed per family. Only MSI is validated end-to-end;
# the rest are exercised here to discover which run to completion.
#
# Usage:
#   ./sweep_protocols.sh [--suite eembc|splash|both] [--family snoop|directory|both]
#                        [--jobs N] [--no-gate] [--eembc-safety S] [--splash-safety S]
#                        [--out DIR]
#   --no-gate : run SPLASH for every protocol even if it failed EEMBC triage
set -u
ROOT="/c/Octopus"; cd "$ROOT"
BIN="$ROOT/build/Octopus_Simulator.exe"
CFGDIR="$ROOT/configuration/SystemConfigurations"
ACTIVE="$CFGDIR/MultiCoreSystem.csv"

SUITE=both; FAMILY=both; JOBS=9; GATE=1
EEMBC_SAFETY=600; SPLASH_SAFETY=36000; OUT=""
while [ $# -gt 0 ]; do case "$1" in
  --suite) SUITE="${2,,}"; shift 2;;
  --family) FAMILY="${2,,}"; shift 2;;
  --jobs) JOBS="$2"; shift 2;;
  --no-gate) GATE=0; shift;;
  --eembc-safety) EEMBC_SAFETY="$2"; shift 2;;
  --splash-safety) SPLASH_SAFETY="$2"; shift 2;;
  --out) OUT="$2"; shift 2;;
  *) echo "unknown arg: $1" >&2; exit 1;;
esac; done
OUT="${OUT:-$ROOT/sweep-protocols}"; mkdir -p "$OUT/logs" "$OUT/configs"
RES="$OUT/results.csv"
echo "family,protocol,suite,bench,status,wall_s,cores_done,rows_c0" > "$RES"

# protocol table: "family|proto|base_preset|L1_proto|L1_fsm|LLC_proto|LLC_fsm"
CONFIGS=(
  "snoop|MSI|MultiCoreSystem_Snoop.csv|SNOOP_MSI|MSI_splitBus_snooping|SNOOP_LLC_MSI|MSI_LLC"
  "snoop|MESI|MultiCoreSystem_Snoop.csv|SNOOP_MESI|MESI_splitBus_snooping|SNOOP_LLC_MESI|MESI_LLC"
  "snoop|MOESI|MultiCoreSystem_Snoop.csv|SNOOP_MOESI|MOESI_splitBus_snooping|SNOOP_LLC_MOESI|MOESI_LLC"
  "snoop|PMSI|MultiCoreSystem_Snoop.csv|SNOOP_PMSI|PMSI|SNOOP_LLC_PMSI|PMSI_LLC"
  "snoop|PMESI|MultiCoreSystem_Snoop.csv|SNOOP_PMESI|PMESI|SNOOP_LLC_PMESI|PMESI_LLC"
  "snoop|PMSIx|MultiCoreSystem_Snoop.csv|SNOOP_PMSI_ASTERISK|PMSI_asterisk|SNOOP_LLC_PMSI_ASTERISK|PMSI_asterisk_LLC"
  "snoop|PMESIx|MultiCoreSystem_Snoop.csv|SNOOP_PMESI_ASTERISK|PMESI_asterisk|SNOOP_LLC_PMESI_ASTERISK|PMESI_asterisk_LLC"
  "directory|MSI|MultiCoreSystem_Directory.csv|DIRECTORY_MSI|MSI_directory|DIRECTORY_LLC_MSI|MSI_LLC_directory"
  "directory|MESI|MultiCoreSystem_Directory.csv|DIRECTORY_MESI|MESI_directory|DIRECTORY_LLC_MESI|MESI_LLC_directory"
  "directory|MOESI|MultiCoreSystem_Directory.csv|DIRECTORY_MOESI|MOESI_directory|DIRECTORY_LLC_MOESI|MOESI_LLC_directory"
)

SPLASH_SKIP="raytrace radiosity radix"   # the 3 giants
FAULT_RE='fault|error|invalid|not recognized|cannot|not found|unfound|missed|segmentation|terminate|abort|assert'

log(){ echo "$*"; }

gen_config(){  # family proto base L1p L1f LLCp LLCf -> writes config, echoes path
  local fam="$1" proto="$2" base="$3" l1p="$4" l1f="$5" llcp="$6" llcf="$7"
  local out="$OUT/configs/${fam}_${proto}.csv"
  sed -E \
    -e "s#^(cache_controller\[\*\]\.protocol_type\(s\),)[^,]*#\1$l1p#" \
    -e "s#^(cache_controller\[\*\]\.fsm_filename\(s\),)[^,]*#\1$l1f#" \
    -e "s#^(llc_controller\.protocol_type\(s\),)[^,]*#\1$llcp#" \
    -e "s#^(llc_controller\.fsm_filename\(s\),)[^,]*#\1$llcf#" \
    "$CFGDIR/$base" > "$out"
  # Snooping MESI/MOESI keep the Exclusive-state copy at the L1 and therefore
  # require the exclusive L1 controller; MSI (no E state) uses the base controller.
  if [ "$fam" = "snoop" ] && { [ "$proto" = "MESI" ] || [ "$proto" = "MOESI" ]; }; then
    sed -i -E "s#^(cache_controller_type\(s\),)[^,]*#\1CacheControllerExclusive#" "$out"
  fi
  echo "$out"
}

# run one benchmark; args: family proto suite benchdir safety -> appends to RES
run_bench(){
  local fam="$1" proto="$2" suite="$3" wp="$4" safety="$5"
  local bench; bench="$(basename "$wp")"
  local nwp; nwp="$(cygpath -m "$wp")/"
  local logf="$OUT/logs/${fam}_${proto}_${suite}_${bench}.log"
  mkdir -p "$wp/newLogger"; rm -f "$wp/newLogger"/*.csv 2>/dev/null
  local s rc wall done_cores c refs rows fin r0 status
  s=$(date +%s)
  timeout "${safety}s" "$BIN" -s MultiCoreSystem -p "workload_path(s)=$nwp" 2>&1 \
      | tr '\r' '\n' | grep -aiE "$FAULT_RE" > "$logf"
  rc=${PIPESTATUS[0]}; wall=$(( $(date +%s) - s ))
  done_cores=0
  for c in 0 1 2 3; do
    [ -f "$wp/trace_C$c.trc.shared" ] || continue
    refs=$(wc -l < "$wp/trace_C$c.trc.shared" 2>/dev/null); refs=${refs:-0}
    rows=$(awk 'END{print NR}' "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); rows=${rows:-0}
    fin=$(grep -c "Average Latency" "$wp/newLogger/LatencyReport_C$c.csv" 2>/dev/null); fin=${fin:-0}
    [ "$fin" -ge 1 ] && [ "$rows" -ge "$refs" ] && done_cores=$((done_cores+1))
  done
  r0=$(awk 'END{print NR}' "$wp/newLogger/LatencyReport_C0.csv" 2>/dev/null); r0=${r0:-0}
  if   [ "$rc" -eq 124 ]; then status=TIMEOUT
  elif [ -s "$logf" ];    then status=FAULT
  elif [ "$done_cores" -eq 4 ]; then status=COMPLETE
  elif [ "$rc" -ne 0 ];   then status="CRASH$rc"
  else status=INCOMPLETE; fi
  echo "$fam,$proto,$suite,$bench,$status,$wall,$done_cores/4,$r0" >> "$RES"
  log "  [$status] $fam/$proto/$suite/$bench wall=${wall}s cores=$done_cores/4"
}

# run a whole suite for one config (benchmarks concurrent up to JOBS); echoes "<n_complete>/<n_total>"
run_suite(){
  local fam="$1" proto="$2" suite="$3" cfg="$4" safety="$5" tr_root benches=() n_run=0
  case "$suite" in
    eembc)  tr_root="$ROOT/BMs/eembc-traces";;
    splash) tr_root="$ROOT/BMs/splash";;
  esac
  for d in "$tr_root"/*/; do
    [ -f "${d}trace_C0.trc.shared" ] || continue
    local b; b="$(basename "$d")"
    if [ "$suite" = splash ]; then case " $SPLASH_SKIP " in *" $b "*) continue;; esac; fi
    benches+=("$d")
  done
  cp -f "$cfg" "$ACTIVE"
  for d in "${benches[@]}"; do
    run_bench "$fam" "$proto" "$suite" "$d" "$safety" &
    n_run=$((n_run+1))
    while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n 2>/dev/null || break; done
  done
  wait
  # verdict: complete count for this config+suite
  local nc; nc=$(awk -F, -v f="$fam" -v p="$proto" -v s="$suite" \
      '$1==f&&$2==p&&$3==s&&$5=="COMPLETE"{c++} END{print c+0}' "$RES")
  echo "$nc/${#benches[@]}"
}

want_family(){ [ "$FAMILY" = both ] || [ "$FAMILY" = "$1" ]; }
want_suite(){ [ "$SUITE" = both ] || [ "$SUITE" = "$1" ]; }

echo "==== protocol sweep | suite=$SUITE family=$FAMILY gate=$GATE jobs=$JOBS @ $(date +%H:%M:%S) ===="
for row in "${CONFIGS[@]}"; do
  IFS='|' read -r fam proto base l1p l1f llcp llcf <<< "$row"
  want_family "$fam" || continue
  cfg="$(gen_config "$fam" "$proto" "$base" "$l1p" "$l1f" "$llcp" "$llcf")"
  echo "---- $fam / $proto ----"
  eembc_ok=1
  if want_suite eembc; then
    v="$(run_suite "$fam" "$proto" eembc "$cfg" "$EEMBC_SAFETY")"
    log "  EEMBC: $v complete"
    [ "${v%%/*}" = "${v##*/}" ] || eembc_ok=0
  fi
  if want_suite splash; then
    if [ "$GATE" -eq 1 ] && want_suite eembc && [ "$eembc_ok" -ne 1 ]; then
      log "  SPLASH: SKIPPED (failed EEMBC triage)"
      echo "$fam,$proto,splash,-,SKIPPED,0,0/4,0" >> "$RES"
    else
      v="$(run_suite "$fam" "$proto" splash "$cfg" "$SPLASH_SAFETY")"
      log "  SPLASH: $v complete"
    fi
  fi
done
echo "==== sweep DONE @ $(date +%H:%M:%S) ===="
