# Multi-hop NoC with modular routing — design plan

Status: proposal, revised 2026-10-09 after a critical review (first draft 2026-09-29); the same
day the real-time arbitration items were deferred to Phase 3 so that the infrastructure is built
first. Nothing here is implemented yet except the per-link tracing noted in §1. The architecture
specification is `docs/NoC_Architecture_Spec.md`. Written against the interconnect
code on `esweek-tutorial`, which is byte-identical to `stable` in this area.

The revision changes five decisions that must be fixed before Phase 1 is coded (§3.1), adds the
correctness properties the design depends on (§3.7, §3.8), adds a validation plan (§5), moves LLC banking into
Phase 2 and keeps the real-time items in Phase 3 (§6), and states where this NoC is meant to do better
than Garnet and BookSim rather than merely catch up (§8).

## 1. What exists today, precisely

The whole mesh/NoC path is ~1 160 lines:

| piece | what it does |
|---|---|
| `Mesh` | one node per `component_ids` entry; `connection_matrix[i]` is node *i*'s adjacency list; one `MeshInterface` per node; ticks the controller on falling edges (`m_clk_period` 50 ns, same as the bus). Despite the name it is a set of **direct links**, in the shipped config the complete graph, not a grid |
| `MeshInterface` | per node: **one TX request queue and one TX data queue per adjacent link**, plus one RX request and one RX data queue; `pushMessage` splits a multicast `to` list into one unicast copy per destination and drops each into the queue of the link that leads *directly* to it |
| `MeshController::Link` | one `Link` per adjacency pair **per class** (`RequestLink` latency 2, `DataLink` latency 5); each link has its own `Arbiter` (FCFS between the two endpoints' TX queues; RR/TDM over the global core slots) and is half-duplex: one message in flight per latency window, either direction |
| `MeshController::step` | per interconnect cycle: for every idle link, elect a message; after `latency` cycles, `send()` places it straight into the destination's RX |
| `NoC` | `Mesh` plus `Switch` nodes. A switch is just another interface. `NoC::cycleProcess` drains every switch's RX and re-pushes each message into its TX **in the same cycle, with no latency and no arbitration of its own** |
| `NoCInterface::m_routing_table` | `map<dest → next-hop link>`, built once at construction: every direct neighbour maps to itself, and for each adjacent *switch*, everything that switch lists maps to the switch |

So a message can traverse at most one switch, because a switch's own routing table only knows its
direct neighbours; a destination behind two switches has no entry. That is the single-hop limit.
Routing is per-source, static, and has no notion of path choice, distance, or congestion.

### Defects in the current code, worth fixing regardless of the plan

1. **Unknown destination is dropped silently.** `noc_pushMessage` does `m_routing_table[to_id]` —
   `operator[]` on a `std::map` inserts 0 for a missing key — finds no link with id 0, pushes
   nothing, and **returns true**. A misconfigured or two-hop destination vanishes without a
   message. It should abort with the destination named.
2. **TX queues are unbounded.** The capacity check is `m_tx_request_buffer.size() <
   m_buffer_max_size`, but `m_tx_request_buffer` is the vector *of per-link queues*, so `.size()`
   is the link count — a constant. The intended check is on the chosen queue's depth. Consequence:
   no back-pressure on the TX side; combined with `exit(0)` on a full RX ("full buffer"), any real
   congestion crashes instead of stalling. Present in both `NoCInterface.cpp` and
   `MeshInterface.cpp`.
3. **Per-hop observability** — *partly addressed on 2026-10-04*: `MeshController` and
   `NoCController` now trace every link crossing as its own `REQ_BUS`/`RESP_BUS` event with the link
   index as component, so the visualizer lanes show the path. The LatencyReport still folds the
   whole route into the two bus columns and records no hop count.
4. **Failures exit with status 0.** Every "full buffer" path calls `exit(0)`, so a crashed run looks
   successful to anything that reads the exit code. Aborts should return non-zero.

## 2. Target

A k-ary n-cube style network of **routers** with flit-level wormhole switching, input-buffered
virtual channels, a pluggable **routing function**, per-hop latency, and credit-based back-pressure;
deadlock-free by construction for the shipped algorithms; deterministic; configured from CSV like
everything else; and reachable by the existing controllers through the unchanged
`CommunicationInterface` contract, so no cache controller or protocol changes.

Explicitly in scope: directory protocols (point-to-point traffic). Deferred to Phase 3: snooping
on a NoC, which needs an ordered broadcast tree (§6).

## 3. Design

### 3.1 Decisions fixed before Phase 1 (the review's "would force rework" list)

1. **Flit-level wormhole, not message-level.** A message is a packet of head, body and tail flits
   over a link of `flit_bytes` width. The head is routed and allocates a VC; body flits follow; the
   tail releases the VC. Buffers are counted in flits and are allowed to be shallower than a packet,
   so a packet blocked downstream keeps occupying upstream buffers and links. That upstream blocking
   is the *indirect interference* term of every real-time wormhole-NoC analysis and the reason the
   model must be flit-level: a message-level model is virtual cut-through by construction and is
   optimistic in exactly the worst case we want to measure. Priority-preemptive VCs and TDM slot
   tables (§3.6) are flit-granular by nature as well. **There is one model, not two**: setting
   `flit_bytes` to the message size gives one flit per packet, which *is* the fast message-level
   model as a configuration rather than a second code path.
2. **Serialization by size, not by class.** Occupancy of a link is the packet's flit count; the
   request/data asymmetry (today's 2 vs 5 ticks) becomes a consequence of message size and link
   width. The old keys `m_request_latency`/`m_data_latency` remain accepted and set the defaults for
   the degenerate single-router configuration so existing CSVs keep running.
3. **Credits travel.** A credit returns to the upstream router over the link and costs the link's
   latency; it is not an instantaneous counter. `credit_latency(i)` defaults to `link_latency`.
   This bounds per-VC throughput the way hardware does; the simplification "instantaneous credits"
   is *not* offered, because it understates worst cases.
4. **Buffers are `[class][vc]` from day one.** Each input port has `vcs_per_class` VCs per message
   class (§3.3), each `vc_depth` flits deep. Default `vcs_per_class = 1`. Escape channels for
   adaptive routing and priority VCs for real-time arbitration need several VCs within one class;
   retrofitting that means rewriting the router core, so the structure is there from the start even
   while only one VC is used.
5. **N message classes from day one.** FIFOs, classifier and credit counters are indexed by a class
   count, never written for a fixed two. Phase 1 runs two classes; Phase 2 adds the third by a
   classifier change and a config line.

### 3.2 Principles

- **Controllers never change** — with one stated obligation: they must keep the admission property
  in §3.8, which the network's deadlock guarantee depends on.
- **Topology is data, routing is a plug-in.** Topologies come from CSV (explicit matrix as today, or
  a generator). Routing algorithms, slot tables and routing tables are selected or loaded by name,
  exactly the way arbiters are.
- **Reuse what works:** `Arbiter` for VC and switch allocation; the `Logger` role/phase scheme for
  per-hop tracing; the task layer's free-running generators as traffic sources (§5).
- **The current single-switch NoC becomes a degenerate case** (one router, star topology, table
  routing, one flit per packet) so existing configs keep running and results stay byte-identical.

### 3.3 Components

```
Topology         builds the graph: routers, links, endpoint→router attachment, coordinates
RoutingAlgorithm nextHop(here, dest, packet) -> output port        [plug-in, by name]
Router           input VCs per port × class, VC + switch allocators, credits, pipeline latency
NetworkInterface endpoint side: unchanged contract; packetizes, splits multicast, per-class RX
NoCController    the per-cycle engine: route → VC allocate → switch allocate → traverse
```

**`Topology`** replaces the hand-written adjacency-only view with three things the router needs:
adjacency, **coordinates** (so dimension-order routing can exist), and an **endpoint map** (which
cache/LLC id hangs off which router; several endpoints may share a router, each taking a local
port). Sources:

- `topology(s)=custom` — today's `connection_matrix`, unchanged.
- `topology(s)=grid2d`, `dims(vi)=k,k` — generated; routers numbered row-major;
  `node_map[<endpoint id>](i)=<router>` attaches endpoints. `torus2d` and `ring` are one-line
  variants. (Named `grid2d`, not `mesh2d`, because the existing `Mesh` class is the direct-link
  topology; see §3.9.)

**`RoutingAlgorithm`** is the modular part:

```cpp
class RoutingAlgorithm {
public:
    virtual int  nextHop(int router, int dest_router, const Packet &p) = 0;  // output port
    virtual bool deadlockFreeOn(const Topology &) const = 0;                  // self-check at init
    virtual Ordering ordering() const = 0;        // what point-to-point order it preserves (§3.7)
};
```

Shipped implementations, in order of delivery:

| name | what | deadlock freedom | ordering |
|---|---|---|---|
| `Table` | static next-hop table; computed at init by BFS shortest paths on the topology, or loaded from CSV (`routing_table[<router>](vi)=dest:port,...`) | not guaranteed — the init check walks the channel-dependency graph and refuses a cyclic table unless escape VCs are enabled | per pair per class, with one VC |
| `XY` | dimension-order for `grid2d`; X then Y | free by construction | per pair per class, with one VC |
| `WestFirst` / turn model | partially adaptive on `grid2d` | free by construction | none across paths |
| `AdaptiveMinimal` | picks among minimal outputs by credit count; needs an escape VC running XY | free given the escape channel | none |

A new algorithm is **one class implementing `nextHop`** and one line in the factory — the mesh
analogue of "add an arbiter", and the better tutorial exercise once the interface exists.

**`Router`** replaces `NoC::Switch`. Per input port: `[class][vc]` flit FIFOs of depth `vc_depth`.
Pipeline per packet: route computation on the head, VC allocation (an `Arbiter` over the heads
wanting the same output VC), then per flit: switch allocation (an `Arbiter` over the input VCs
wanting the same output port) and link traversal. A flit moves when its VC holds the output VC,
the switch allocator picks it, and a credit is available. `router_latency` ticks for the pipeline,
`link_latency` ticks on the wire. Cut-through follows from flit granularity: the head leaves
before the tail has arrived.

**Arbitration semantics.** The existing arbiters elect over cores on a bus. In a router the
competitors at an output are input VCs, while TDM and priority (§3.6) want ownership by core, which
`Message::owner` provides on every packet. The allocators therefore run the arbiter over input VCs
and pass the owning core of each candidate, so FCFS/RR work over VCs and TDM/priority work over
cores; `electOldestOwned` is reused unchanged.

**Back-pressure** is credits end to end: an endpoint injects a flit only when its router's input VC
has a credit; a router forwards only when the next VC has one; credits return over the link with
`credit_latency`. The two `exit(0)` "full buffer" paths become genuine stalls, and any remaining
abort returns non-zero (§1, defect 4).

### 3.4 Message classes and virtual networks

Today request and data already travel on separate `Link`s (two physical networks). Keep that
separation as **virtual networks** sharing physical links, with three classes, because a directory
protocol has three message types whose cyclic dependency is the textbook protocol deadlock:

| class | messages | how to classify |
|---|---|---|
| REQUEST | GetS/GetM/PutS/PutM from L1s to the LLC | `kind` ∈ {K_GETS, K_GETM, K_PUTS, K_PUTM}, else `data == NULL` and destination is the LLC |
| FORWARD | Fwd_GetS/Fwd_GetM/Inv from the LLC to L1s | `kind` ∈ {K_FWD, K_INV}, else `data == NULL` and source is the LLC |
| RESPONSE | data, InvAck, Put_Ack | `kind` ∈ {K_RESP, K_SUPPLY, K_FILL, K_ACK, ...} or `data != NULL` |

`Message::kind` already carries the raw-trace kinds; the classifier keys on `kind` and falls back
to the data/direction rule only for untagged messages (and logs that it did). Each class has its
own VCs per input port and its own RX queue at the endpoint (§3.8). This is the separation the
earlier "forward-request virtual channel" note asked for on the bus; it is cheaper to get right in
a new NoC than to retrofit.

With XY routing, one VC per class and the admission property of §3.8, the shipped configuration is
deadlock-free without adaptive machinery. `Table` routing gets the init-time cycle check instead.

### 3.5 Timing and clocking

Unchanged framing: the NoC is a `ClockedObj` stepping on falling edges. **Units: all NoC latencies
are in interconnect ticks, two per core cycle** (`m_clk_period` 50 vs 100), so the defaults below
cost 1.5 core cycles per hop plus serialization. This is the one place a user types a latency that
is not in core cycles; the manual says so next to the keys.

```
interconnect.topology(s),grid2d
interconnect.dims(vi),2,2
interconnect.routing(s),XY
interconnect.flit_bytes(i),16          # link width; = message size gives one flit per packet
interconnect.router_latency(i),2       # route + allocate, ticks
interconnect.link_latency(i),1         # per hop, ticks
interconnect.credit_latency(i),1       # credit return, ticks (default = link_latency)
interconnect.vcs_per_class(i),1
interconnect.vc_depth(i),4             # flits, per VC
interconnect.node_map[0](i),0          # L1 0 on router 0 ... LLC (id 10) on router 3
```

A 2×2 grid with four L1s and one LLC then costs 1–3 hops per packet plus its flit count, instead of
a flat 2 or 5, which is the first result worth showing.

### 3.6 Real-time arbitration (Phase 3, deferred by decision of 2026-10-09)

These are the same one-class-plus-factory-line pattern as XY and are what the group publishes on.
They are deferred until the infrastructure (Phases 1 and 2) is built and validated; the hooks that
avoid rework (`Candidate.owner`, `Route.vc_mask`) are in the specification now. What ships then:

- **`TDM_XY`**: a slot table per link, in flits, loaded from CSV
  (`slot_table[<link>](vi)=core,core,...`) or generated round-robin; a flit of core *c* traverses
  the link only in *c*'s slot. The bound per hop is a slot count, which is what the Phase 3 bound
  calculator sums.
- **Priority VC allocation**: `vcs_per_class ≥ 2` with a priority per core or per criticality level
  (`vc_priority[<core>](i)`); the switch allocator is priority-preemptive at flit granularity, the
  model used by the Shi–Burns family of analyses.
- **Bounded-wait arbiters**: the existing RR/FCFS classes already apply; nothing new.

### 3.7 Ordering

The directory protocol relies on point-to-point order in places (the stale-PutS misread on the bus
was exactly an order assumption). The plan states the property instead of hoping for it:

- **Guaranteed**: with a deterministic routing algorithm and `vcs_per_class = 1`, packets of one
  class between one source–destination pair are delivered in injection order (FIFO VCs on a fixed
  path). This covers `Table` and `XY` as shipped (and `TDM_XY` when it arrives in Phase 3).
- **Not guaranteed**: across classes (by design), across VCs within a class, and under adaptive
  routing. `RoutingAlgorithm::ordering()` declares which, and the protocol presets declare what
  they require (`requires_ordering(s),pair_per_class`); a run whose network cannot provide what the
  protocol requires refuses to start.

### 3.8 Deadlock freedom, end to end

Three virtual networks remove cyclic buffer dependencies **inside** the network. The argument only
closes if the sinks cooperate, so two more things are part of the design:

- **The network interface keeps the classes apart up to the controller**: one RX queue per class
  (today's single request RX would put a FORWARD behind a stalled REQUEST). The controller's
  processing queue still merges them; what matters is the admission rule.
- **The admission property controllers must keep**: a controller always consumes RESPONSE and
  FORWARD packets independent of its ability to issue or admit REQUESTs. Octopus does this today by
  force-admitting responses and forwards (the directory force-admit fix); this plan *names* it as
  the property the NoC relies on and adds a unit test that fails if a future controller change
  breaks it.
- **Two checks, two scopes**: the `Table` init check covers routing cycles only; the
  **protocol-and-network checker** (Phase 2) reads the protocol CSVs, classifies every message type
  the tables can emit, and verifies that the network's classes and the admission property close
  the protocol's dependency cycle. A green routing check must not be read as a protocol guarantee.

### 3.9 Naming

The existing `Mesh` class is a set of direct links, which in the shipped configuration happens to
be a complete graph; anyone reading "Mesh" expects a grid. The generated grid topology is therefore
`grid2d`, the manual and `SUPPORTED_CONFIGURATIONS.md` describe `Mesh` as "direct links", and the
old class name is kept for configuration compatibility.

### 3.10 Observability

- `Logger::Role::NOC` with `comp = router id`; the **head** flit stamps ENTER (into an input VC) and
  SERVICE (won VC allocation), the **tail** flit stamps EXIT (off the output link), so a packet's
  per-hop record spans its whole traversal. The visualizer gets one lane per router from the
  existing resource-lane code, draws the selected request's route across the lanes, and adds a
  port-contention heatmap keyed by the same request ids as the coherence transition table.
- LatencyReport: the "Request Bus" and "Response Bus" columns become the *sum over hops*, and three
  mechanism-tracker columns are added per request: **hop count**, **NoC wait** (time waiting for a
  VC or the switch, summed over hops) and **NoC transfer** (time on links, summed), plus a
  **NoC Ahead** counter (packets, and their owning cores, served ahead of this one at contended
  ports), in the style of the existing `Resp Ahead` columns. The event-path tiling check
  (`[EVENT-PATH]`) must still sum to Total — the validation that catches an accounting mistake.
- `OCTOPUS_HANG_DUMP` extends to routers: per-port, per-VC occupancy and credits. A routing deadlock
  then shows as a ring of full VCs, which is exactly what one wants to see.

### 3.11 Multicast

`to` lists (an LLC invalidating several sharers) are split into unicast packets at the network
interface, as now. A multicast tree is a later optimisation; correctness never depends on it.

## 4. Open questions

1. **LLC banking** is no longer open: it moves into Phase 2 (§6), because a single LLC on a grid is a
   hotspot and makes every result a statement about one router. It needs one controller-side change,
   a home map in the L1's choice of `m_shared_memory_id` by address interleaving.
2. **Snooping on a NoC** moves to Phase 3 as the research item it is (§6).
3. **Flit granularity** is decided (§3.1): flit-level, with the message-level model as a setting.
4. **Memory controllers on the network.** With banked LLCs, memory controllers at edge routers and
   `bus[1]` carried by the NoC is the natural next step; not scheduled.

## 5. Validation

Protocol runs are the last test, not the first. In order:

1. **Unloaded latency.** A lone packet costs exactly `hops × (router_latency + link_latency) +
   (flits − 1)` ticks. One assertion, run on every topology.
2. **Network-only harness.** Endpoints are free-running generators from the task layer (no caches),
   patterns uniform-random, hotspot and transpose. The latency-versus-injection-rate curve for a 4×4
   XY grid under uniform random must saturate where BookSim's does for the same parameters; this is
   the one external reference and the test that validates credits, VC allocation and the switch
   model together.
3. **Degenerate case.** One router, `Table` routing, `flit_bytes` = message size: results
   byte-identical to today's single-switch NoC on directory MSI EEMBC.
4. **Deadlock checks.** A deliberately cyclic routing table is refused at init; a protocol table
   with a message type outside the three classes is refused by the Phase 2 checker; the admission
   unit test fails on a controller that stops force-admitting responses.
5. **Ordering.** A test that injects two same-class packets per pair under every shipped algorithm
   and checks delivery order against the declared `ordering()`.
6. **Determinism.** Two runs of every test are byte-identical; ties in allocation are broken by ids,
   never by pointer order.
7. **Protocol runs.** Directory MSI EEMBC 10/10 on a 2×2 grid; 4×4 with the L1s and LLC banks at
   the corners; worst-case latency versus placement that makes sense.

## 6. Phasing

**Phase 0 — fixes to the existing code (hours).** Abort with the destination named on an unknown
destination; fix the TX depth check in both interfaces; make every abort return non-zero; document
the single-hop limit and the "direct links" meaning of `Mesh` in `SUPPORTED_CONFIGURATIONS.md`.
Not a prerequisite for Phase 1 (the switch code is deleted by it); worth doing if the current NoC
stays in use for more than a few weeks.

**Phase 1 — the network (four to five weeks).** `Topology` (`custom` + `grid2d`), flit-level
`Router` with `[class][vc]` buffers, travelling credits, `NoCController` engine, `Table` and `XY`
routing with `ordering()`, per-class endpoint RX, two classes (request/data, as today), the
unloaded-latency test, the network-only harness with the BookSim curve, and the degenerate-case
equality. The earlier two-week estimate assumed message granularity and instantaneous credits.

**Phase 2 — protocol-grade and real-time (two to three weeks).** Three classes and the admission
unit test; `Role::NOC` stamps, the three tracker columns and `NoC Ahead`, router lanes, route drawing
and the contention heatmap in the visualizer; tiling check green; `HANG_DUMP` router view;
the protocol-and-network deadlock checker (§3.8); LLC banking by address interleaving. Acceptance:
validation items 4–7.

**Phase 3 — research surface.** First the real-time arbitration of §3.6, `TDM_XY` and priority
VCs, then the **bound calculator**: for `TDM_XY` and priority VCs, compute the
analytical per-hop bound from the same terms the simulator stamps (slots, blocking, serialization)
and have the sweep harness plot bound against measured per core — the timing-validation pillar
applied to the network. Then turn-model and adaptive routing with an escape VC; `torus2d`; memory
controllers on the network; and **snooping over a NoC through an ordered broadcast tree**, which the
CSV protocols make tractable because the protocol does not change, only delivery order has to be
guaranteed.

## 7. Relationship to the tutorial

None for ESWEEK — this is weeks, not days. Once Phase 1 exists, "add a routing algorithm" is a
better 45-minute extension exercise than "add an arbiter": same shape (one class, one factory
line), and the result is visible as a different route on the visualizer's router lanes.

## 8. Where this NoC is meant to do better than Garnet and BookSim

Not on flit-level fidelity: Garnet and BookSim are the references there and §5 uses BookSim as the
yardstick. On five things they structurally lack:

1. **Every network latency is attributable, per hop and per cause**, with worst cases per core, the
   way every other stage in Octopus already is (§3.10).
2. **Predictable arbitration as a shipped option**, not a research fork: TDM slot tables and
   priority VCs beside XY, swept from the command line (§3.6, Phase 3). No living open simulator offers an
   Æthereal-style network inside a full coherent hierarchy.
3. **Bound against measured, per hop** (Phase 3): the simulator emits the terms the analysis sums,
   so a bound and its measured worst case are plotted together.
4. **Network and protocol checked together**: a static checker reads the CSV protocol and the
   network configuration and closes the deadlock argument that Ruby leaves to the designer (§3.8).
5. **Seeing it**: router lanes, routes and contention keyed to the same request ids as the coherence
   transition table, the only NoC view tied to the protocol event that caused the traffic.

Plus two that come free from the existing design: routing and slot tables are CSV data like
everything else, and a whole EEMBC suite still runs in seconds, so placement, routing and
arbitration sweeps are minutes rather than nights.
