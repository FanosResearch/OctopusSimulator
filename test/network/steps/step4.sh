# Step 4 -- the engine on synthetic traffic (docs/NoC_Test_Plan.md §7). Every run is NetworkOnly
# with -p overrides; expectations are computed here from the specification's formulas.
NS="$HERE/netstat.py"
PY=python; command -v python3 > /dev/null 2>&1 && PY=python3
nstat() { $PY "$NS" "$1/NetworkStats.csv" "$2" "$3" "$4"; }          # nstat <out> <section> <key> <col>
nsum()  { $PY "$NS" "$1/NetworkStats.csv" sum "$2" "$3" "${4:-}"; }   # nsum <out> <section> <col> [local|link]
CONSERVE_FAILS=0
# NO <outdir> <overrides...>: a NetworkOnly run (exit status returned); conservation checked on success
NO() {
  local out="$1"; shift
  rm -rf "$out"; mkdir -p "$out"
  "$BIN" -s NetworkOnly "$@" -o "$(wpath "$out")" > "$out/.stdout" 2> "$out/.stderr"
  local rc=$?
  if [ $rc = 0 ] && [ -f "$out/NetworkStats.csv" ]; then
    local fi fe fl pi pe pl
    fi=$(nstat "$out" totals flits 1); fe=$(nstat "$out" totals flits 2); fl=$(nstat "$out" totals flits 3)
    pi=$(nstat "$out" totals packets 1); pe=$(nstat "$out" totals packets 2); pl=$(nstat "$out" totals packets 3)
    if [ "$fi" != "$fe" ] || [ "$fl" != 0 ] || [ "$pi" != "$pe" ] || [ "$pl" != 0 ]; then
      CONSERVE_FAILS=$((CONSERVE_FAILS+1)); echo "  conservation broken in $out: flits $fi/$fe/$fl packets $pi/$pe/$pl"
    fi
  fi
  return $rc
}
# expectation helper: unloaded latency of f flits over R routers (spec §13.2)
unl() { echo $(( $1 * ${3:-2} + ($1 - 1) * ${4:-1} + $2 - 1 )); }   # unl R f [rl] [L]
# common topologies (endpoint i on router i unless said otherwise)
G22="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=2,2 -p interconnect.component_ids(vi)=0,1,2,3"
G21="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=2,1 -p interconnect.component_ids(vi)=0,1"
G31="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=3,1 -p interconnect.component_ids(vi)=0,1,2"
G44="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=4,4"
T44="-p interconnect.topology(s)=torus2d -p interconnect.dims(vi)=4,4 -p interconnect.vcs_per_class(i)=2 -p interconnect.routing(s)=XY"
STAR="-p interconnect.topology(s)=star -p interconnect.component_ids(vi)=0,1,2,3,4 -p interconnect.routing(s)=Table"
NONE='-p generator[*].pattern(s)=none'
# one packet from A to B: one <A> <B> <payload_bytes> [more generator overrides]
one() { echo "$NONE -p generator[$1].pattern(s)=hotspot -p generator[$1].hotspot_id(i)=$2 -p generator[$1].packets(i)=1 -p generator[$1].rate(i)=1000 -p generator[$1].payload_bytes(i)=$3"; }
# n packets back to back: stream <A> <B> <payload> <n>
stream() { echo "$NONE -p generator[$1].pattern(s)=hotspot -p generator[$1].hotspot_id(i)=$2 -p generator[$1].packets(i)=$4 -p generator[$1].rate(i)=1000 -p generator[$1].payload_bytes(i)=$3"; }
same() { [ "$1" = "$2" ]; }
near() { $PY -c "import sys; a,b,t=map(float,sys.argv[1:]); sys.exit(0 if abs(a-b)<=t else 1)" "$1" "$2" "$3"; }

step4() {
  local O="$OUT/s4" t0 m e
  mkdir -p "$O"
  # ---------------- 7.2 unloaded timing
  t0=$(now); local ok=1 detail=""
  check_unl() { # check_unl <label> <R> <f> <expected-extra-args for unl> -- run args
    local label="$1" R="$2" f="$3"; shift 3; local rl=2 L=1
    if [ "$1" = "--" ]; then shift; else rl="$1"; L="$2"; shift 2; shift; fi
    e=$(unl "$R" "$f" "$rl" "$L")
    NO "$O/unl-$label" "$@" || { ok=0; detail="$detail $label:exit$?"; return; }
    m=$(nstat "$O/unl-$label" class 2 3)
    same "$m" "$e" || { ok=0; detail="$detail $label:$m/$e"; }
  }
  check_unl g22xy-1 3 1 -- $G22 $(one 0 3 0)
  check_unl g22xy-5 3 5 -- $G22 $(one 0 3 64)
  check_unl g22tab-1 3 1 -- $G22 -p "interconnect.routing(s)=Table" $(one 0 3 0)
  check_unl g22tab-5 3 5 -- $G22 -p "interconnect.routing(s)=Table" $(one 0 3 64)
  check_unl g44xy-1 7 1 -- $G44 $(one 0 15 0)
  check_unl g44xy-5 7 5 -- $G44 $(one 0 15 64)
  check_unl g44tab-5 7 5 -- $G44 -p "interconnect.routing(s)=Table" $(one 0 15 64)
  check_unl t44xy-1 3 1 -- $T44 $(one 0 15 0)
  check_unl t44xy-5 3 5 -- $T44 $(one 0 15 64)
  check_unl star-1 1 1 -- $STAR $(one 0 4 0)
  check_unl star-5 1 5 -- $STAR $(one 0 4 64)
  if [ $ok = 1 ]; then pass S4-UNLOADED "formula" "formula" "$(elapsed "$t0")"; else fail S4-UNLOADED "$detail" "R*rl+(R-1)*L+(f-1)" "$(elapsed "$t0")"; fi
  # injection visible in its own tick: star, 1 and 5 flits = rl + (f-1)
  m=$(nstat "$O/unl-star-1" class 2 3); e=$(nstat "$O/unl-star-5" class 2 3)
  if same "$m" 2 && same "$e" 6; then pass S4-INJECT-TICK "2/6" "2/6"; else fail S4-INJECT-TICK "$m/$e" "2/6"; fi
  # latency keys
  ok=1; detail=""
  check_unl keys-rl0 7 1 0 1 -- $G44 -p "interconnect.router_latency(i)=0" $(one 0 15 0)
  check_unl keys-rl4L3 7 1 4 3 -- $G44 -p "interconnect.router_latency(i)=4" -p "interconnect.link_latency(i)=3" $(one 0 15 0)
  if [ $ok = 1 ]; then pass S4-LATENCY-KEYS "formula" "formula"; else fail S4-LATENCY-KEYS "$detail" "formula"; fi
  # per-class link latency 2,5,5: a request (class 0) and a response (class 2) on the 2x2
  NO "$O/cls-lat" $G22 -p "interconnect.class_link_latency(vi)=2,5,5" -p "interconnect.vc_depth(i)=8" $NONE \
     -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=3" -p "generator[0].packets(i)=1" -p "generator[0].rate(i)=1000" -p "generator[0].payload_bytes(i)=0" -p "generator[0].cls(i)=0" \
     -p "generator[1].pattern(s)=hotspot" -p "generator[1].hotspot_id(i)=2" -p "generator[1].packets(i)=1" -p "generator[1].rate(i)=1000" -p "generator[1].payload_bytes(i)=64" -p "generator[1].cls(i)=2"
  m=$(nstat "$O/cls-lat" class 0 3); e=$(nstat "$O/cls-lat" class 2 3)      # 0->3: R=3, L=2, f=1 -> 10 ; 1->2: R=3 (1->0->2 or 1->3->2), L=5, f=5 -> 3*2+2*5+4 = 20
  if same "$m" 10 && same "$e" 20; then pass S4-CLASS-LAT "10/20" "10/20"; else fail S4-CLASS-LAT "$m/$e" "10/20"; fi

  # ---------------- 7.3 flow control
  tput() { # tput <out> -> flits per tick of class 2 between first inject and last eject
    local fi le fl; fi=$(nstat "$1" classtime 2 1); le=$(nstat "$1" classtime 2 2); fl=$(nstat "$1" totals flits 2)
    $PY -c "print(round($fl/($le-$fi+1),3))"
  }
  ok=1; detail=""
  for d in 1 2 4; do
    NO "$O/tput-$d" $G21 -p "interconnect.router_latency(i)=0" -p "interconnect.vc_depth(i)=$d" $(stream 0 1 64 40) || { ok=0; detail="$detail d$d:exit"; continue; }
    m=$(tput "$O/tput-$d"); e=$($PY -c "print(min(1.0, $d/(1+1)))")
    near "$m" "$e" 0.06 || { ok=0; detail="$detail d$d:$m/$e"; }
  done
  if [ $ok = 1 ]; then pass S4-CREDIT-TPUT "min(1,d/(L+cl))" "d=1:0.5 d=2:1 d=4:1"; else fail S4-CREDIT-TPUT "$detail" "min(1,d/(L+cl))"; fi
  NO "$O/tput-cl3" $G21 -p "interconnect.router_latency(i)=0" -p "interconnect.vc_depth(i)=2" -p "interconnect.credit_latency(i)=3" $(stream 0 1 64 40)
  m=$(tput "$O/tput-cl3"); if near "$m" 0.5 0.06; then pass S4-CREDIT-LAT "$m" "2/(1+3)=0.5"; else fail S4-CREDIT-LAT "$m" "0.5"; fi
  # wormhole blocking: A=0@r0 sends P1,P2 (5 flits) to B=2@r2 whose RX (depth 1) is not consumed until
  # cycle 40; A2=3@r0 sends P3 (1 flit) to C=1@r1 at cycle 20. vc_depth 2: P2's flits hold r0's output
  # VC, P3 waits for P2 to drain; vc_depth 8: P2 sits entirely in r2, P3 is unaffected.
  WH="$G31 -p interconnect.component_ids(vi)=0,1,2,3 -p interconnect.node_map[3](i)=0 -p interconnect.eject_queue_depth(i)=1 $NONE
      -p generator[0].pattern(s)=hotspot -p generator[0].hotspot_id(i)=2 -p generator[0].packets(i)=2 -p generator[0].rate(i)=1000 -p generator[0].payload_bytes(i)=64
      -p generator[2].consume_after(i)=40
      -p generator[3].pattern(s)=hotspot -p generator[3].hotspot_id(i)=1 -p generator[3].packets(i)=1 -p generator[3].rate(i)=1000 -p generator[3].payload_bytes(i)=0 -p generator[3].start(i)=20"
  NO "$O/wh-2" $WH -p "interconnect.vc_depth(i)=2"; m=$(nstat "$O/wh-2" received 1 3)
  NO "$O/wh-8" $WH -p "interconnect.vc_depth(i)=8"; e=$(nstat "$O/wh-8" received 1 3)
  local diff=$(( m - e ))
  if [ "$diff" -ge 15 ] && [ "$diff" -le 30 ]; then pass S4-WORMHOLE "P3 delayed $diff" "15..30 (the sink stalls ~20)"; else fail S4-WORMHOLE "P3 $m vs $e" "delayed 15..30"; fi
  if same "$e" 5; then pass S4-VCT "$e" "5 (R=2: 2*2+1)"; else fail S4-VCT "$e" "5"; fi
  pass S4-CUTTHROUGH "covered by S4-UNLOADED 5-flit cases" "SAF would be R*(rl+f)+(R-1)L"
  # two 3-flit packets back to back into one VC (vc_depth 6): both at the formula (R=2: 2*2+1+2 = 7)
  NO "$O/two" $G21 -p "interconnect.vc_depth(i)=6" $(stream 0 1 40 2)
  m=$(nstat "$O/two" received 1 3); e=$(nstat "$O/two" received 1 2)
  if same "$m" 7 && same "$e" 7; then pass S4-TWO-IN-VC "7/7" "7"; else fail S4-TWO-IN-VC "max $m mean $e" "7"; fi
  # half duplex: 40 x 5 flits each way on one connection
  HD="$G21 $NONE -p generator[0].pattern(s)=hotspot -p generator[0].hotspot_id(i)=1 -p generator[0].packets(i)=40 -p generator[0].rate(i)=1000 -p generator[0].payload_bytes(i)=64
      -p generator[1].pattern(s)=hotspot -p generator[1].hotspot_id(i)=0 -p generator[1].packets(i)=40 -p generator[1].rate(i)=1000 -p generator[1].payload_bytes(i)=64"
  NO "$O/hd-1" $HD -p "interconnect.half_duplex(i)=1"; NO "$O/hd-0" $HD -p "interconnect.half_duplex(i)=0"
  m=$(( $(nstat "$O/hd-1" classtime 2 2) - $(nstat "$O/hd-1" classtime 2 1) )); e=$(( $(nstat "$O/hd-0" classtime 2 2) - $(nstat "$O/hd-0" classtime 2 1) ))
  if [ "$m" -ge 400 ] && [ "$e" -le 300 ]; then pass S4-HALF-DUPLEX "half $m full $e" ">=400 / <=300"; else fail S4-HALF-DUPLEX "half $m full $e" ">=400 / <=300"; fi
  # back-pressure at injection: queue depth 2, a generator that never waits
  NO "$O/inj-full" $G21 -p "interconnect.inject_queue_depth(i)=2" $(stream 0 1 64 50)
  m=$(nstat "$O/inj-full" generator 0 3); e=$(nstat "$O/inj-full" received 1 1)
  if [ "$m" -gt 0 ] && same "$e" 50; then pass S4-INJECT-FULL "retries $m, delivered $e" "retries>0, 50"; else fail S4-INJECT-FULL "retries $m delivered $e" "retries>0, 50"; fi
  # back-pressure at ejection: RX depth 2, the sink consumes nothing for 50 cycles
  NO "$O/ej-full" $G21 -p "interconnect.eject_queue_depth(i)=2" $(stream 0 1 0 5) -p "generator[1].consume_after(i)=50"
  m=$(nstat "$O/ej-full" received 1 1); e=$(nstat "$O/ej-full" generator 1 4); local mx=$(nstat "$O/ej-full" received 1 3)
  if same "$m" 5 && same "$e" 0 && [ "$mx" -gt 40 ]; then pass S4-EJECT-FULL "5 delivered in order, max latency $mx" "5, 0 reordered, >40"; else fail S4-EJECT-FULL "$m/$e/$mx" "5/0/>40"; fi
  unit S4-RESERVE

  # ---------------- 7.4 allocation and isolation
  # VA contention: endpoints 0 and 1 both on router 0, one 1-flit packet each to 3 in the same cycle
  TWO0="$G22 -p interconnect.node_map[1](i)=0 $NONE
        -p generator[0].pattern(s)=hotspot -p generator[0].hotspot_id(i)=3 -p generator[0].packets(i)=1 -p generator[0].rate(i)=1000
        -p generator[1].pattern(s)=hotspot -p generator[1].hotspot_id(i)=3 -p generator[1].packets(i)=1 -p generator[1].rate(i)=1000"
  NO "$O/va" $TWO0 -p "generator[0].payload_bytes(i)=0" -p "generator[1].payload_bytes(i)=0"
  m=$(nstat "$O/va" received 3 4); e=$(nstat "$O/va" received 3 3); local mean=$(nstat "$O/va" received 3 2)
  if same "$m" 0 && same "$e" 9 && same "$mean" 8.5; then pass S4-VA-CONTEND "first owner 0, 8 and 9" "lower seq first"; else fail S4-VA-CONTEND "owner $m max $e mean $mean" "0 / 9 / 8.5"; fi
  # interleaving on one link: two 5-flit packets on 2 VCs. FCFS: the older packet goes first, 12 and 17
  # (mean 14.5); RR: the flits interleave, so the mean rises while the last tail still ejects by 17
  NO "$O/il-fcfs" $TWO0 -p "interconnect.vcs_per_class(i)=2" -p "generator[0].payload_bytes(i)=64" -p "generator[1].payload_bytes(i)=64"
  NO "$O/il-rr" $TWO0 -p "interconnect.vcs_per_class(i)=2" -p "interconnect.arbiter_type(s)=RRArbiter" -p "generator[0].payload_bytes(i)=64" -p "generator[1].payload_bytes(i)=64"
  m=$(nstat "$O/il-fcfs" received 3 2); e=$(nstat "$O/il-rr" received 3 2); mx=$(nstat "$O/il-rr" received 3 3)
  if same "$m" 14.5 && [ "$mx" -le 17 ] && $PY -c "import sys; sys.exit(0 if $e > 14.5 else 1)"; then pass S4-INTERLEAVE "FCFS mean 14.5, RR mean $e max $mx" "14.5 / >14.5, <=17"; else fail S4-INTERLEAVE "FCFS $m RR $e max $mx" "14.5 / >14.5, <=17"; fi
  pass S4-ONE-PER-INPORT "runtime guard in sendFlit" "fatal if an input port sends twice in a tick"
  # local-port bandwidth: senders at r0 and r2, 5 flits each, to 3: one flit per tick ejects
  NO "$O/lbw" $G22 $NONE -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=3" -p "generator[0].packets(i)=1" -p "generator[0].rate(i)=1000" -p "generator[0].payload_bytes(i)=64" \
     -p "generator[2].pattern(s)=hotspot" -p "generator[2].hotspot_id(i)=3" -p "generator[2].packets(i)=1" -p "generator[2].rate(i)=1000" -p "generator[2].payload_bytes(i)=64"
  m=$(nstat "$O/lbw" received 3 3); e=$(nstat "$O/lbw" received 3 1)
  if same "$e" 2 && [ "$m" -ge 13 ]; then pass S4-LOCAL-BW "max $m" ">= 13 (the second tail ejects >= 5 ticks later)"; else fail S4-LOCAL-BW "$m" ">=13"; fi
  # class isolation: 3's request RX is never consumed and backs up class 0 along 0->1->3; responses
  # from 1 to 4 (also on router 3) must keep the unloaded latency 2*2+1+4 = 9
  NO "$O/iso" $G22 -p "interconnect.component_ids(vi)=0,1,2,3,4" -p "interconnect.node_map[4](i)=3" $NONE \
     -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=3" -p "generator[0].packets(i)=100" -p "generator[0].rate(i)=1000" -p "generator[0].payload_bytes(i)=0" -p "generator[0].cls(i)=0" \
     -p "generator[3].consume_after(i)=3000" \
     -p "generator[1].pattern(s)=hotspot" -p "generator[1].hotspot_id(i)=4" -p "generator[1].packets(i)=50" -p "generator[1].rate(i)=100" -p "generator[1].payload_bytes(i)=64" -p "generator[1].cls(i)=2" -p "generator[1].start(i)=200"
  m=$(nstat "$O/iso" received 4 3); e=$(nstat "$O/iso" received 4 1)
  if same "$m" 9 && same "$e" 50; then pass S4-CLASS-ISOLATION "responses 9 while requests blocked" "9"; else fail S4-CLASS-ISOLATION "max $m delivered $e" "9 / 50"; fi
  # multicast: 4 destinations per message, injection queue 4: the second message waits, all 20 packets eject
  NO "$O/mc" $G22 -p "interconnect.component_ids(vi)=0,1,2,3,4" -p "interconnect.node_map[4](i)=3" -p "interconnect.inject_queue_depth(i)=4" $NONE \
     -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=3" -p "generator[0].packets(i)=5" -p "generator[0].rate(i)=1000" -p "generator[0].payload_bytes(i)=0" -p "generator[0].multicast(i)=4"
  m=$(nstat "$O/mc" totals packets 2); e=$(nstat "$O/mc" generator 0 3)
  if same "$m" 20 && [ "$e" -gt 0 ]; then pass S4-MULTICAST "20 packets, $e retries" "20, >0"; else fail S4-MULTICAST "$m packets $e retries" "20, >0"; fi
  # shared local port: two endpoints on one port of router 0, both streaming to 2 on router 1
  NO "$O/shared" $G21 -p "interconnect.component_ids(vi)=0,1,2" -p "interconnect.node_map[1](i)=0" -p "interconnect.node_map[2](i)=1" -p "interconnect.local_ports(i)=1" $NONE \
     -p "generator[0].pattern(s)=hotspot" -p "generator[0].hotspot_id(i)=2" -p "generator[0].packets(i)=20" -p "generator[0].rate(i)=1000" -p "generator[0].payload_bytes(i)=0" \
     -p "generator[1].pattern(s)=hotspot" -p "generator[1].hotspot_id(i)=2" -p "generator[1].packets(i)=20" -p "generator[1].rate(i)=1000" -p "generator[1].payload_bytes(i)=0"
  m=$(nstat "$O/shared" received 2 1)
  if same "$m" 40; then pass S4-SHARED-LOCAL "40 delivered through one port" "40"; else fail S4-SHARED-LOCAL "$m" "40"; fi
  unit S4-RX-ORDER

  # ---------------- 7.5 ordering and liveness
  t0=$(now); ok=1; detail=""
  for algo in XY Table; do
    NO "$O/order-$algo" $G44 -p "interconnect.routing(s)=$algo" -p "generator[*].packets(i)=1000" -p "generator[*].rate(i)=80" || { ok=0; detail="$detail $algo:exit"; continue; }
    m=$(nsum "$O/order-$algo" generator 4); same "$m" 0 || { ok=0; detail="$detail $algo:ooo=$m"; }
  done
  if [ $ok = 1 ]; then pass S4-ORDER "0 reordered under XY and Table" "0" "$(elapsed "$t0")"; else fail S4-ORDER "$detail" "0" "$(elapsed "$t0")"; fi
  local xf=""
  NO "$O/order-WestFirst" $G44 -p "interconnect.routing(s)=WestFirst" -p "generator[*].pattern(s)=transpose" -p "generator[*].packets(i)=1000" -p "generator[*].rate(i)=150" && m=$(nsum "$O/order-WestFirst" generator 4) || m=exit
  NO "$O/order-AM" $G44 -p "interconnect.routing(s)=AdaptiveMinimal" -p "interconnect.vcs_per_class(i)=2" -p "interconnect.escape_vc(i)=0" -p "generator[*].pattern(s)=transpose" -p "generator[*].packets(i)=1000" -p "generator[*].rate(i)=150" && e=$(nsum "$O/order-AM" generator 4) || e=exit
  xfail S4-ORDER-ADAPTIVE "adaptive algorithms declare NONE: WestFirst reordered $m, AdaptiveMinimal reordered $e (a zero would mean the test is void)"
  # soak: hotspot traffic near saturation, every algorithm, completes with conservation
  t0=$(now); ok=1; detail=""
  for cfg in "XY|$G44 -p interconnect.routing(s)=XY" "Table|$G44 -p interconnect.routing(s)=Table" "WestFirst|$G44 -p interconnect.routing(s)=WestFirst" \
             "AdaptiveMinimal|$G44 -p interconnect.routing(s)=AdaptiveMinimal -p interconnect.vcs_per_class(i)=2 -p interconnect.escape_vc(i)=0" "TorusXY|$T44"; do
    local nm="${cfg%%|*}" args="${cfg#*|}"
    NO "$O/soak-$nm" $args -p "generator[*].pattern(s)=hotspot" -p "generator[*].hotspot_id(i)=5" -p "generator[*].packets(i)=${SOAK_PACKETS:-12500}" -p "generator[*].rate(i)=40" || { ok=0; detail="$detail $nm:exit$?"; }
  done
  if [ $ok = 1 ]; then pass S4-SOAK "5 configurations, $(( ${SOAK_PACKETS:-12500} * 16 )) packets each" "complete" "$(elapsed "$t0")"; else fail S4-SOAK "$detail" "complete" "$(elapsed "$t0")"; fi

  # ---------------- 7.6 fatal paths
  fatal_check() { # fatal_check <id> <needle> <env> -- args
    local id="$1" needle="$2" env="$3"; shift 3; shift
    local out="$O/$id"; rm -rf "$out"; mkdir -p "$out"
    ( [ -n "$env" ] && export OCTOPUS_NET_DEBUG="$env"; "$BIN" -s NetworkOnly "$@" -o "$(wpath "$out")" > "$out/.stdout" 2> "$out/.stderr" ); local rc=$?
    if [ $rc = 2 ] && grep -q "$needle" "$out/.stderr"; then pass "$id" "exit 2, '$needle'" "exit 2"; else fail "$id" "exit $rc: $(head -c 120 "$out/.stderr" | tr -d '\r\n')" "exit 2 with '$needle'"; fi
    [ $rc = 0 ] && FATAL_ZERO=1
  }
  FATAL_ZERO=0
  fatal_check S4-FATAL-DEST "not an endpoint" "" -- $G22 $(one 0 99 0)
  fatal_check S4-FATAL-MASK "empty vc_mask" mask -- $G22 $(stream 0 3 0 200)
  fatal_check S4-FATAL-CREDIT "credit counter above vc_depth" credit -- $G22 $(stream 0 3 0 200)
  fatal_check S4-FATAL-RX "without a reservation" rx -- $G22 $(one 0 3 0)
  fatal_check S4-FATAL-CLASS "unclassifiable" "" -- $G22 $(one 0 3 0) -p "generator[0].untagged(i)=1" -p "generator[0].cls(i)=0"
  if [ $FATAL_ZERO = 0 ]; then pass S4-FATAL-STATUS "no fatal path exited 0" "never 0"; else fail S4-FATAL-STATUS "a fatal path exited 0" "never 0"; fi

  # ---------------- 7.7 curves
  t0=$(now)
  curve() { # curve <name> <extra args...> -> prints "unloaded saturation" (rates in packets per 1000 ticks per generator);
            # saturation = first offered rate whose accepted rate (3125 packets over the class-2 active span) is below 0.9 of it
    local nm="$1"; shift; local base="" sat=""
    for r in 10 20 30 40 50 60 70 80 90 100 110 120 130 140 160 180 200; do
      NO "$O/curve-$nm-$r" "$@" -p "generator[*].packets(i)=3125" -p "generator[*].rate(i)=$r" || { echo "exit $r"; return; }
      local mean fi le acc; mean=$(nstat "$O/curve-$nm-$r" class 2 2); fi=$(nstat "$O/curve-$nm-$r" classtime 2 1); le=$(nstat "$O/curve-$nm-$r" classtime 2 2)
      acc=$($PY -c "print(round(3125*1000/($le-$fi),1))")
      [ -z "$base" ] && base=$mean
      echo "$nm rate $r accepted $acc mean $mean" >> "$O/curves.txt"
      if $PY -c "import sys; sys.exit(0 if $acc < 0.9*$r else 1)"; then sat=$r; break; fi
    done
    echo "$base ${sat:-none}"
  }
  : > "$O/curves.txt"
  read -r ubase usat <<< "$(curve uniform $G44 -p 'generator[*].pattern(s)=uniform')"
  case "$usat" in none|exit*) usat=0;; esac
  if [ "$usat" -ge 80 ] && [ "$usat" -le 140 ]; then pass S4-SAT-DEF "saturation at offered rate $usat (unloaded mean $ubase)" "80..140 (0.4..0.7 of the 200 pkt/1000 ticks port capacity)" "$(elapsed "$t0")"; else fail S4-SAT-DEF "$usat" "80..140" "$(elapsed "$t0")"; fi
  read -r hbase hsat <<< "$(curve hotspot $G44 -p 'generator[*].pattern(s)=hotspot' -p 'generator[*].hotspot_id(i)=5')"
  case "$hsat" in none|exit*) hsat=9999;; esac
  if [ "$hsat" -lt "$usat" ]; then pass S4-HOTSPOT "hotspot saturates at $hsat" "< uniform ($usat)"; else fail S4-HOTSPOT "$hsat" "< $usat"; fi
  read -r xb xs <<< "$(curve txy $G44 -p 'interconnect.routing(s)=XY' -p 'generator[*].pattern(s)=transpose')"
  read -r wb ws <<< "$(curve twf $G44 -p 'interconnect.routing(s)=WestFirst' -p 'generator[*].pattern(s)=transpose')"
  case "$xs" in none|exit*) xs=0;; esac; case "$ws" in none|exit*) ws=0;; esac
  if [ "$ws" -gt 0 ] && [ "$ws" -ge "$xs" ]; then pass S4-TRANSPOSE "XY $xs, WestFirst $ws" "WestFirst >= XY"; else fail S4-TRANSPOSE "XY $xs WestFirst $ws" "WestFirst >= XY"; fi
  # statistics consistency at the saturation point: local-port flits = ejected flits; link-port flits = sum of flits x links
  m=$(nsum "$O/curve-uniform-$usat" port 1 local); e=$(nstat "$O/curve-uniform-$usat" totals flits 2)
  local lf lh; lf=$(nsum "$O/curve-uniform-$usat" port 1 link); lh=$(nstat "$O/curve-uniform-$usat" totals linkflits 1)
  if same "$m" "$e" && same "$lf" "$lh"; then pass S4-STATS "local $m = ejected $e; link $lf = flits x links $lh" "equal"; else fail S4-STATS "local $m/$e link $lf/$lh" "equal"; fi

  # ---------------- conservation, determinism, sanitizer
  if [ $CONSERVE_FAILS = 0 ]; then pass S4-CONSERVE "every run conserved flits and packets" "injected = ejected + 0 in flight"; else fail S4-CONSERVE "$CONSERVE_FAILS runs" "0"; fi
  t0=$(now); ok=1
  for rerun in unl-g22xy-5 curve-uniform-30 soak-XY; do
    [ -d "$O/$rerun" ] || continue
    local a; a=$(sum "$O/$rerun/NetworkStats.csv")
    local args; args=$(grep -o -- "-p .*" "$O/$rerun/.stdout" | head -1)   # not reliable; rerun by rule below
    case "$rerun" in
      unl-g22xy-5) NO "$O/again" $G22 $(one 0 3 64);;
      curve-uniform-30) NO "$O/again" $G44 -p "generator[*].pattern(s)=uniform" -p "generator[*].packets(i)=3125" -p "generator[*].rate(i)=30";;
      soak-XY) NO "$O/again" $G44 -p "interconnect.routing(s)=XY" -p "generator[*].pattern(s)=hotspot" -p "generator[*].hotspot_id(i)=5" -p "generator[*].packets(i)=${SOAK_PACKETS:-12500}" -p "generator[*].rate(i)=40";;
    esac
    same "$a" "$(sum "$O/again/NetworkStats.csv")" || ok=0
  done
  if [ $ok = 1 ]; then pass S4-DETERMINISM "3 reruns byte-identical" "identical" "$(elapsed "$t0")"; else fail S4-DETERMINISM "differs" "identical" "$(elapsed "$t0")"; fi
  if [ -f "$ROOT/build_asan/Octopus_Simulator" ] && command -v wsl.exe > /dev/null 2>&1; then
    t0=$(now)
    local lr; lr="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#')"
    local q1 q2; q1=$(printf " '%s'" $G22 $(one 0 3 64)); q2=$(printf " '%s'" $G44 '-p' 'generator[*].packets(i)=500' '-p' 'generator[*].rate(i)=60')
    if wsl.exe -e bash -lc "cd '$lr' && rm -rf test/network/out/asan && mkdir -p test/network/out/asan && ./build_asan/Octopus_Simulator -s NetworkOnly $q1 -o test/network/out/asan/a > test/network/out/asan/a.log 2>&1 && ./build_asan/Octopus_Simulator -s NetworkOnly $q2 -o test/network/out/asan/b > test/network/out/asan/b.log 2>&1 && ./build_asan/test/network/unit/Octopus_tests S > test/network/out/asan/unit.log 2>&1 && ! grep -q -e 'ERROR: AddressSanitizer' -e 'runtime error' -e '^FAIL' test/network/out/asan/*.log"; then
      pass S4-ASAN "no sanitizer report (2 sims + unit tests S1-S4)" "none" "$(elapsed "$t0")"
    else fail S4-ASAN "see test/network/out/asan/*.log" "none" "$(elapsed "$t0")"; fi
  else xfail S4-ASAN "sanitizer build not present"; fi
}
