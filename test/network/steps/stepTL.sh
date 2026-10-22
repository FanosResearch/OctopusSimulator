# Step TL -- TileLink on the network (docs/TileLink_Plan.md s5.3 I-C, s6 TL-NET-7): 5 and 7 classes keyed
# on the TileLink channel, per-channel admission, the class-order check. NetworkOnly with the
# generators' TileLink mode (tl_mix: weights of A..E); every run uses -p overrides only.
TL_PY=python; command -v python3 > /dev/null 2>&1 && TL_PY=python3
tl_stat() { $TL_PY "$HERE/netstat.py" "$1/NetworkStats.csv" "$2" "$3" "$4"; }      # tl_stat <out> <section> <key1> <col>
tl_run() { # tl_run <outdir> <overrides...>: a NetworkOnly run, exit status returned
  local out="$1"; shift
  rm -rf "$out"; mkdir -p "$out"
  timeout 600 "$BIN" -s NetworkOnly "$@" -o "$(wpath "$out")" > "$out/.stdout" 2> "$out/.stderr"
}
# tl_drained <out> <classes>: every class carried packets, nothing in flight, every reservation balanced
tl_drained() {
  local out="$1" n="$2" c
  [ -f "$out/NetworkStats.csv" ] || { echo "no NetworkStats"; return 1; }
  for ((c = 0; c < n; c++)); do
    local pk; pk=$(tl_stat "$out" class $c 1)
    [ "${pk:-0}" -gt 0 ] 2>/dev/null || { echo "class $c carried ${pk:-nothing}"; return 1; }
    local rx tx taken given
    rx=$(tl_stat "$out" reserve $c 1); tx=$(tl_stat "$out" reserve $c 2); taken=$(tl_stat "$out" reserve $c 3); given=$(tl_stat "$out" reserve $c 4)
    { [ "$rx" = 0 ] && [ "$tx" = 0 ] && [ "$taken" = "$given" ] && [ "$taken" -gt 0 ]; } || { echo "class $c reservations rx $rx tx $tx taken $taken released $given"; return 1; }
  done
  local pi pe pl
  pi=$(tl_stat "$out" totals packets 1); pe=$(tl_stat "$out" totals packets 2); pl=$(tl_stat "$out" totals packets 3)
  { [ "$pi" = "$pe" ] && [ "$pl" = 0 ]; } || { echo "packets $pi injected $pe ejected $pl in flight"; return 1; }
  return 0
}
# tl_sides <out>: on 7 classes the memory-side endpoints 12..15 receive only A_out/D_out (3, 4), the others only the inner classes
tl_sides() {
  awk -F, '$1=="epclass" { mem = ($2 >= 12 && $2 <= 15); outer = ($3 == 3 || $3 == 4); if (mem != outer) { print "endpoint " $2 " received class " $3; bad = 1 } } END { exit bad }' "$1/NetworkStats.csv"
}

stepTL() {
  local O="$OUT/tl" t0 m r; mkdir -p "$O"
  unit TL-
  local G44="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=4,4 -p interconnect.routing(s)=XY"
  local GEN="-p generator[*].tl_mix(vi)=3,2,2,4,1 -p generator[*].rate(i)=120 -p generator[*].packets(i)=400 -p generator[*].payload_bytes(i)=64"
  local MEM="-p interconnect.memory_ids(vi)=12,13,14,15"
  local TINY="-p interconnect.inject_queue_depth(i)=1 -p interconnect.eject_queue_depth(i)=1 -p interconnect.vc_depth(i)=2"
  # ---------------- TL-NET-5: five classes A < B < C < D < E, memory off the network
  t0=$(now)
  tl_run "$O/n5" $G44 -p "interconnect.num_classes(i)=5" $GEN; r=$?
  if [ $r = 0 ] && m=$(tl_drained "$O/n5" 5); then pass TL-NET-5 "5/5 classes carry and drain, reservations 0" "every class, balanced" "$(elapsed "$t0")"
  else fail TL-NET-5 "exit $r ${m:-} $(head -c 160 "$O/n5/.stderr" | tr -d '\r\n')" "every class carries and drains, reservations 0" "$(elapsed "$t0")"; fi
  # ---------------- TL-NET-7: seven classes, endpoints 12..15 memory-side (the outer link: A_out, D_out)
  t0=$(now)
  tl_run "$O/n7" $G44 -p "interconnect.num_classes(i)=7" $MEM $GEN; r=$?
  if [ $r = 0 ] && m=$(tl_drained "$O/n7" 7) && m=$(tl_sides "$O/n7"); then pass TL-NET-7 "7/7 classes carry and drain, reservations 0, outer classes on the memory side only" "every class, balanced" "$(elapsed "$t0")"
  else fail TL-NET-7 "exit $r ${m:-} $(head -c 160 "$O/n7/.stderr" | tr -d '\r\n')" "every class carries and drains, reservations 0" "$(elapsed "$t0")"; fi
  # the same under queues of 1 and VCs of 2 flits
  t0=$(now)
  tl_run "$O/n7tiny" $G44 -p "interconnect.num_classes(i)=7" $MEM $GEN $TINY; r=$?
  if [ $r = 0 ] && m=$(tl_drained "$O/n7tiny" 7) && m=$(tl_sides "$O/n7tiny"); then pass TL-NET-7-TINY "7/7 drain with queues of 1" "every class, balanced" "$(elapsed "$t0")"
  else fail TL-NET-7-TINY "exit $r ${m:-} $(head -c 160 "$O/n7tiny/.stderr" | tr -d '\r\n')" "every class carries and drains" "$(elapsed "$t0")"; fi
  # ---------------- TL-NET-DET: the 5- and 7-class runs again, NetworkStats identical
  t0=$(now)
  tl_run "$O/n5b" $G44 -p "interconnect.num_classes(i)=5" $GEN
  tl_run "$O/n7b" $G44 -p "interconnect.num_classes(i)=7" $MEM $GEN
  if [ -f "$O/n5b/NetworkStats.csv" ] && [ "$(sum "$O/n5/NetworkStats.csv")" = "$(sum "$O/n5b/NetworkStats.csv")" ] && [ "$(sum "$O/n7/NetworkStats.csv")" = "$(sum "$O/n7b/NetworkStats.csv")" ]; then
    pass TL-NET-DET identical identical "$(elapsed "$t0")"
  else fail TL-NET-DET "NetworkStats differ" "identical" "$(elapsed "$t0")"; fi
  # ---------------- negative cases: each exits 2 with its message
  tl_fatal() { # tl_fatal <id> <needle> <args...>
    local id="$1" needle="$2"; shift 2
    tl_run "$O/$id" "$@"; local rc=$?
    if [ $rc = 2 ] && grep -q "$needle" "$O/$id/.stderr"; then pass "$id" "exit 2, '$needle'" "exit 2"; else fail "$id" "exit $rc: $(head -c 160 "$O/$id/.stderr" | tr -d '\r\n')" "exit 2 with '$needle'"; fi
  }
  tl_fatal TL-NEG-3CLASS "TileLink message on a 3-class network" $G44 $GEN
  tl_fatal TL-NEG-UNTAGGED "without a TileLink channel on a 5-class" $G44 -p "interconnect.num_classes(i)=5"
  tl_fatal TL-NEG-4CLASS "num_classes must be 2, 3, 5 or 7" $G44 -p "interconnect.num_classes(i)=4"
  tl_fatal TL-NEG-MEMIDS "apply to num_classes 7 only" $G44 -p "interconnect.num_classes(i)=5" $MEM $GEN
  tl_fatal TL-NEG-CROSS "between a memory-side endpoint and an inner one" $G44 -p "interconnect.num_classes(i)=7" $MEM $GEN -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=12"
}
