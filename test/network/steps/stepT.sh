# Step T -- topologies and routing as plug-ins (docs/NoC_Topology_Plugin_Plan.md s5): the unit
# tests with prefix T (port order, routing_via, registry, lattice ports, flags, the dependency
# check over candidate outputs, the features built on them), then NetworkOnly runs of the new
# topologies and algorithms. Expectations come from the unloaded-latency formula (spec s13.2)
# and from conservation (every injected flit ejected, nothing left in flight), checked by NO()
# from step4.sh.
stepT() {
  unit T
  local O="$OUT/sT" t0 m e ok detail
  mkdir -p "$O"
  local R8="-p interconnect.topology(s)=ring -p interconnect.dims(vi)=8 -p interconnect.component_ids(vi)=0,1,2,3,4,5,6,7 -p interconnect.routing(s)=auto"
  local M3="-p interconnect.topology(s)=mesh3d -p interconnect.dims(vi)=4,2,2 -p interconnect.routing(s)=auto"
  local T3="-p interconnect.topology(s)=torus3d -p interconnect.dims(vi)=3,3,3 -p interconnect.vcs_per_class(i)=2 -p interconnect.routing(s)=auto"

  # ---------------- unloaded latency on the new lattices: R*rl + (R-1)*L + (f-1)
  t0=$(now); ok=1; detail=""
  tunl() { # tunl <label> <R> <f> -- run args
    local label="$1" R="$2" f="$3"; shift 4
    e=$(unl "$R" "$f")
    NO "$O/unl-$label" "$@"; local rc=$?; [ $rc = 0 ] || { ok=0; detail="$detail $label:exit$rc"; return; }
    m=$(nstat "$O/unl-$label" class 2 3)
    same "$m" "$e" || { ok=0; detail="$detail $label:$m/$e"; }
  }
  # ring of 8, two VCs: 0 -> 4 is 4 hops either way (5 routers); 0 -> 6 goes counter-clockwise (3 routers)
  tunl ring-04-1 5 1 -- $R8 -p interconnect.vcs_per_class\(i\)=2 $(one 0 4 0)
  tunl ring-06-5 3 5 -- $R8 -p interconnect.vcs_per_class\(i\)=2 $(one 0 6 64)
  # mesh3d 4x2x2: router 0 (0,0,0) to router 15 (3,1,1) crosses 3+1+1 links, 6 routers
  tunl m3-1 6 1 -- $M3 $(one 0 15 0)
  tunl m3-5 6 5 -- $M3 $(one 0 15 64)
  # torus3d 3x3x3: 0 (0,0,0) to 13 (1,1,1) is one hop per dimension, 4 routers; to 26 (2,2,2) wraps on every dimension, also 4
  tunl t3-13 4 1 -- $T3 -p interconnect.component_ids\(vi\)=0,13,26 -p interconnect.node_map[13]\(i\)=13 -p interconnect.node_map[26]\(i\)=26 $(one 0 13 0)
  tunl t3-26 4 5 -- $T3 -p interconnect.component_ids\(vi\)=0,13,26 -p interconnect.node_map[13]\(i\)=13 -p interconnect.node_map[26]\(i\)=26 $(one 0 26 64)
  if [ $ok = 1 ]; then pass T4-UNLOADED "formula" "formula" "$(elapsed "$t0")"; else fail T4-UNLOADED "$detail" "R*rl+(R-1)*L+(f-1)" "$(elapsed "$t0")"; fi

  # ---------------- auto routing: XY on a ring with two VCs, Table with one (k = 3, no cycle)
  t0=$(now)
  NO "$O/auto-ring2" $R8 -p interconnect.vcs_per_class\(i\)=2 -p generator[*].packets\(i\)=200; local a=$?
  NO "$O/auto-ring1" -p interconnect.topology\(s\)=ring -p interconnect.dims\(vi\)=3 -p interconnect.component_ids\(vi\)=0,1,2 -p interconnect.routing\(s\)=auto -p generator[*].packets\(i\)=200; local b=$?
  NO "$O/auto-ring8-1vc" $R8 -p generator[*].packets\(i\)=10; local c=$?
  if [ $a = 0 ] && [ $b = 0 ] && [ $c = 2 ] && grep -q "routing Table is not deadlock-free" "$O/auto-ring8-1vc/.stdout" "$O/auto-ring8-1vc/.stderr" 2>/dev/null; then
    pass T4-AUTO-RING "ring8 XY ok, ring3 Table ok, ring8 1 VC refused" "same" "$(elapsed "$t0")"
  else fail T4-AUTO-RING "rc $a/$b/$c" "0/0/2 with the Table refusal" "$(elapsed "$t0")"; fi

  # ---------------- loaded runs complete with conservation (no deadlock)
  t0=$(now); ok=1; detail=""; local before=$CONSERVE_FAILS
  soak() { local label="$1"; shift; NO "$O/soak-$label" "$@"; local rc=$?; [ $rc = 0 ] || { ok=0; detail="$detail $label:exit$rc"; }; }
  soak ring8 $R8 -p interconnect.vcs_per_class\(i\)=2 -p generator[*].rate\(i\)=200
  soak mesh3d $M3 -p generator[*].rate\(i\)=150
  soak torus3d $T3 -p generator[*].rate\(i\)=150
  soak adapt-grid -p interconnect.routing\(s\)=AdaptiveMinimal -p interconnect.vcs_per_class\(i\)=2 -p interconnect.escape_vc\(i\)=1 -p generator[*].rate\(i\)=100
  soak adapt-torus -p interconnect.topology\(s\)=torus2d -p interconnect.routing\(s\)=AdaptiveMinimal -p interconnect.vcs_per_class\(i\)=3 -p interconnect.escape_vc\(i\)=0 -p generator[*].rate\(i\)=150
  soak adapt-ring8 $R8 -p interconnect.routing\(s\)=AdaptiveMinimal -p interconnect.vcs_per_class\(i\)=3 -p interconnect.escape_vc\(i\)=0 -p generator[*].rate\(i\)=200
  soak adapt-mesh3d $M3 -p interconnect.routing\(s\)=AdaptiveMinimal -p interconnect.vcs_per_class\(i\)=2 -p interconnect.escape_vc\(i\)=1 -p generator[*].rate\(i\)=150
  local tm; for tm in WestFirst NorthLast NegativeFirst OddEven; do
    soak "$tm" -p interconnect.routing\(s\)=$tm -p generator[*].rate\(i\)=100
    soak "$tm-transpose" -p interconnect.routing\(s\)=$tm -p generator[*].pattern\(s\)=transpose -p generator[*].rate\(i\)=100
  done
  [ $CONSERVE_FAILS = $before ] || { ok=0; detail="$detail conservation"; }
  if [ $ok = 1 ]; then pass T4-SOAK "all complete, conserved" "same" "$(elapsed "$t0")"; else fail T4-SOAK "$detail" "all complete, conserved" "$(elapsed "$t0")"; fi

  # ---------------- the old torus escape network (one escape VC) is refused
  NO "$O/adapt-torus-2vc" -p interconnect.topology\(s\)=torus2d -p interconnect.routing\(s\)=AdaptiveMinimal -p interconnect.vcs_per_class\(i\)=2 -p interconnect.escape_vc\(i\)=1 -p generator[*].packets\(i\)=10
  if [ $? = 2 ] && grep -q "two escape VCs" "$O/adapt-torus-2vc/.stdout" "$O/adapt-torus-2vc/.stderr" 2>/dev/null; then pass T5-ADAPT-TORUS-REFUSED
  else fail T5-ADAPT-TORUS-REFUSED "accepted or other error" "refused: two escape VCs"; fi

  # ---------------- router options: switch_latency adds a stage per router, router_latency[r] overrides one router
  t0=$(now); ok=1; detail=""
  local G44="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=4,4"
  tlat() { # tlat <label> <expected> -- run args
    local label="$1" want="$2"; shift 3
    NO "$O/lat-$label" "$@"; local rc=$?; [ $rc = 0 ] || { ok=0; detail="$detail $label:exit$rc"; return; }
    m=$(nstat "$O/lat-$label" class 2 3); same "$m" "$want" || { ok=0; detail="$detail $label:$m/$want"; }
  }
  # 0 -> 15 on 4x4 XY: 7 routers, 6 links. switch_latency 1: 7*(2+1) + 6 + 0 = 27
  tlat sl1 27 -- $G44 -p interconnect.switch_latency\(i\)=1 $(one 0 15 0)
  # router 3 (on the XY path 0,1,2,3,7,11,15) at latency 6: 6*2 + 6 + 6 = 24
  tlat rl3 24 -- $G44 -p interconnect.router_latency[3]\(i\)=6 $(one 0 15 0)
  # input_first gives the same unloaded latency: 7*2 + 6 + 4 = 24 for 5 flits
  tlat inputfirst 24 -- $G44 -p interconnect.sa_allocator\(s\)=input_first $(one 0 15 64)
  if [ $ok = 1 ]; then pass T4-ROUTER-OPTIONS "formula" "formula" "$(elapsed "$t0")"; else fail T4-ROUTER-OPTIONS "$detail" "formula" "$(elapsed "$t0")"; fi

  # ---------------- loaded runs of the oblivious algorithms, input_first and the new traffic patterns
  t0=$(now); ok=1; detail=""; before=$CONSERVE_FAILS
  local alg; for alg in O1TURN Valiant ROMM; do
    soak "$alg" -p interconnect.routing\(s\)=$alg -p interconnect.vcs_per_class\(i\)=2 -p generator[*].rate\(i\)=100
  done
  soak romm-mesh3d $M3 -p interconnect.routing\(s\)=ROMM -p interconnect.vcs_per_class\(i\)=2 -p generator[*].rate\(i\)=100
  soak inputfirst-rr -p interconnect.sa_allocator\(s\)=input_first -p interconnect.arbiter_type\(s\)=RRArbiter -p generator[*].rate\(i\)=150
  local pat; for pat in bitcomp bitrev shuffle tornado randperm; do soak "pat-$pat" -p generator[*].pattern\(s\)=$pat -p generator[*].rate\(i\)=100; done
  soak onoff -p generator[*].injection\(s\)=onoff -p generator[*].on_to_off\(i\)=50 -p generator[*].off_to_on\(i\)=50 -p generator[*].rate\(i\)=400
  [ $CONSERVE_FAILS = $before ] || { ok=0; detail="$detail conservation"; }
  if [ $ok = 1 ]; then pass T4-SOAK-2 "all complete, conserved" "same" "$(elapsed "$t0")"; else fail T4-SOAK-2 "$detail" "all complete, conserved" "$(elapsed "$t0")"; fi

  # ---------------- permutation patterns: every endpoint hears from exactly its partner (16 endpoints, 4x4)
  t0=$(now)
  local bad; bad=$($PY - "$O" <<'PYEOF'
import csv, sys, os
O = sys.argv[1]
def rot(i, b=4): return ((i << 1) | (i >> (b - 1))) & 15
def rev(i): return int(format(i, '04b')[::-1], 2)
def torn(i): x, y = i % 4, i // 4; return ((x + 1) % 4) + 4 * ((y + 1) % 4)
send = {'bitcomp': lambda i: 15 - i, 'bitrev': rev, 'shuffle': rot, 'tornado': torn}
errs = []
for pat, f in send.items():
    rows = list(csv.reader(open(os.path.join(O, 'soak-pat-' + pat, 'NetworkStats.csv'))))
    got = {int(r[1]): (int(r[4]), int(r[7])) for r in rows if r and r[0] == 'received'}
    for j in range(16):
        senders = [i for i in range(16) if f(i) == j and f(i) != i]
        n, first = got.get(j, (0, -1))
        if senders and (n == 0 or first != senders[0]): errs.append(f"{pat}:{j}<-{first}/{senders}")
        if not senders and n != 0: errs.append(f"{pat}:{j} got {n}")
print(' '.join(errs))
PYEOF
)
  if [ -z "$bad" ]; then pass T4-PATTERNS "partners as defined" "same" "$(elapsed "$t0")"; else fail T4-PATTERNS "$bad" "partners as defined" "$(elapsed "$t0")"; fi

  # ---------------- randperm is a single cycle: every generator sends, every endpoint receives
  local zs zr
  zs=$(awk -F, '$1=="generator" && $5==0' "$O/soak-pat-randperm/NetworkStats.csv" | wc -l)
  zr=$(awk -F, '$1=="received" && $5==0' "$O/soak-pat-randperm/NetworkStats.csv" | wc -l)
  if [ "$zs" = 0 ] && [ "$zr" = 0 ]; then pass TF-RANDPERM-NO-FIXED-POINT "all send and receive" "same"
  else fail TF-RANDPERM-NO-FIXED-POINT "$zs silent senders, $zr silent receivers" "0, 0"; fi

  # ---------------- refusals with status 2 and the reason: a malformed router id in a key, a bit
  # pattern on 12 endpoints
  ok=1; detail=""
  refused() { # refused <label> <text> -- run args
    local label="$1" text="$2"; shift 3
    NO "$O/ref-$label" "$@"; local rc=$?
    [ $rc = 2 ] && grep -q "$text" "$O/ref-$label/.stderr" || { ok=0; detail="$detail $label:rc$rc"; }
  }
  refused key-x "router_latency\[x\]: the part in brackets must be a router id" -- -p interconnect.router_latency[x]\(i\)=3 -p generator[*].packets\(i\)=10
  refused key-3x "routing_via\[3x\]: the part in brackets must be a router id" -- -p interconnect.routing_via[3x]\(vi\)=2 -p generator[*].packets\(i\)=10
  refused bitcomp12 "needs a power-of-two number of endpoints, got 12" -- -p interconnect.dims\(vi\)=4,3 -p interconnect.component_ids\(vi\)=0,1,2,3,4,5,6,7,8,9,10,11 -p generator[*].pattern\(s\)=bitcomp -p generator[*].packets\(i\)=10
  if [ $ok = 1 ]; then pass TF-REFUSALS "status 2 with the reason" "same"; else fail TF-REFUSALS "$detail" "status 2 with the reason"; fi

  # ---------------- the stats file records the new options
  if grep -q "^options,,1,-1,1,output_first,1,FCFSArbiter$" "$O/lat-sl1/NetworkStats.csv" && grep -q "^routerlatency,3,,,6," "$O/lat-rl3/NetworkStats.csv"
  then pass TF-STATS-OPTIONS "options and routerlatency rows" "same"
  else fail TF-STATS-OPTIONS "rows missing or wrong" "options and routerlatency rows"; fi
}
