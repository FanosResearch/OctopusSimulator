#!/usr/bin/env bash
# slam_protect.sh -- OV2SLAM (gem5 full system) protected from the ATP GPU/DPU aggressors by
# Octopus's predictable mechanisms, on the Jetson Nano-like preset (MultiCoreSystem_gem5_nano,
# 1.43 GHz cores, the 2x-rate V1_02 clip).
#
#   sweeps/slam_protect.sh <runs-dir> <run>...      launch the named runs, a memory gate between them
#   sweeps/slam_protect.sh <runs-dir> list          print the run names and their parameters
#
# Domains: CPU = Octopus requesters 0-7 (L1I 0-3, L1D 4-7), ATP = 8-9. Splits 3/4 : 1/4.
# Two platforms:
#   A (nano_v102_*)    MCsim's default DDR4 mapping: DRAM bank bits 13-16 lie inside the LLC's
#                      page-colour bits 12-16, so colour and DRAM-bank partitioning coincide.
#   B (nano_v102_B_*)  mcsim_bank_shift(i)=4: bank fields read from bits 17-20 (the bank changes
#                      every 128 KiB), so colour and DRAM bank partition independently.
# Each mechanism is compared with the baseline of its own platform.
#
# Env: GEM5 (gem5.opt with Octopus+ATP), LIB (libOctopus.so dir, put on LD_LIBRARY_PATH),
#      RES (gem5 resources), MIN_FREE_GB (memory gate, default 22), GAP (s between launches, 600),
#      EXTRA (more fs_arm.py arguments, e.g. --max-ticks 20000000000 for a 20 ms smoke test),
#      MAX_GEM5 (launch only while fewer gem5.opt processes run, default 6).
# A full run with the fixed library (stable a31d38aa) stays near 13 GB; before that fix the
# ATP runs leaked to 21-30 GB.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEM5="${GEM5:-$ROOT/../gem5-slam/build/ARM/gem5.opt}"
LIB="${LIB:-$ROOT/build-remap}"
RES="${RES:-$HOME/gem5-resources}"
MIN_FREE_GB="${MIN_FREE_GB:-22}"; GAP="${GAP:-600}"; EXTRA="${EXTRA:-}"; MAX_GEM5="${MAX_GEM5:-6}"
OUT="$1"; shift

LB='llc_controller[*].m_data_handler'
# Partitions are numbered from 0 with no gap: the loader stops at the first missing partition[k].
# parts <mechanism>... -> the --octopus-param arguments, numbered in the order given
parts() {
  local k=0 m
  for m in "$@"; do
    case "$m" in
      way)    printf '%q ' --octopus-param "$LB.partition[$k].scheme(s)=way"       --octopus-param "$LB.partition[$k].owners(s)=0-7:0-11;8-9:12-15" ;;
      mshr)   printf '%q ' --octopus-param "$LB.partition[$k].scheme(s)=mshr"      --octopus-param "$LB.partition[$k].owners(s)=0-7:0-23;8-9:24-31" ;;
      colour) printf '%q ' --octopus-param "$LB.partition[$k].scheme(s)=colour"    --octopus-param "$LB.partition[$k].owners(s)=0-7:0-23;8-9:24-31" ;;
      dram)   printf '%q ' --octopus-param "$LB.partition[$k].scheme(s)=dram_bank" --octopus-param "$LB.partition[$k].owners(s)=0-7:0-11;8-9:12-15" ;;
    esac
    k=$((k + 1))
  done
  case " $* " in *" colour "*|*" dram "*) printf '%q ' --octopus-param 'page_placer(i)=1' ;; esac
}
RR=$(printf '%q ' --octopus-param "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter" --octopus-param "llc_controller[*].arbiter_type(s)=RRArbiter")
B=$(printf '%q ' --octopus-param 'mcsim_bank_shift(i)=4')

args() {  # run name -> its fs_arm.py arguments beyond the common ones
  case "$1" in
    nano_v102_base)        ;;
    nano_v102_atp)         echo --atp ;;
    nano_v102_atp_way)     echo "--atp $(parts way)" ;;
    nano_v102_atp_mshr)    echo "--atp $(parts mshr)" ;;
    nano_v102_atp_rr)      echo "--atp $RR" ;;
    nano_v102_B_base)      echo "$B" ;;
    nano_v102_B_atp)       echo "--atp $B" ;;
    nano_v102_B_colour)    echo "--atp $B $(parts colour)" ;;
    nano_v102_B_dram)      echo "--atp $B $(parts dram)" ;;
    nano_v102_B_all)       echo "--atp $B $RR $(parts way mshr colour dram)" ;;
    nano_v102_B_all_solo)  echo "$B $RR $(parts way mshr colour dram)" ;;
    *) echo "unknown run $1" >&2; exit 2 ;;
  esac
}
ALL="nano_v102_base nano_v102_atp nano_v102_atp_way nano_v102_atp_mshr nano_v102_atp_rr nano_v102_B_base nano_v102_B_atp nano_v102_B_colour nano_v102_B_dram nano_v102_B_all nano_v102_B_all_solo"
if [ "${1:-}" = list ]; then for r in $ALL; do echo "$r: $(args $r)"; done; exit 0; fi

free_gb() { awk '/MemAvailable/ { print int($2 / 1048576) }' /proc/meminfo; }
first=1
for r in "$@"; do
  a="$(args "$r")"
  [ $first = 1 ] || sleep "$GAP"
  while [ "$(free_gb)" -lt "$MIN_FREE_GB" ] || [ "$(pgrep -x gem5.opt | wc -l)" -ge "$MAX_GEM5" ]; do sleep 120; done
  first=0
  d="$OUT/$r"; mkdir -p "$d"; echo "$a" > "$d/params.txt"
  ( cd "$ROOT" && eval "LD_LIBRARY_PATH=$LIB /usr/bin/time -f '%e s wall' -o $d/wall.txt $GEM5 -re -d $d gem5/configs/fs_arm.py \
      --resources $RES --image arm-ubuntu-22.04-trim-v102.img --clip 3s --octopus-config MultiCoreSystem_gem5_nano \
      --cpu-freq 1.43GHz --checkpoint $RES/v102/ov2_start_trim_3s_v102_4s5_rate2 $a $EXTRA" > "$d/launch.log" 2>&1 & )
  echo "$(date '+%F %T') launched $r ($(free_gb) GB free)"
done
