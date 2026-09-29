# Multi-hop NoC with modular routing — design plan

Status: proposal (2026-09-29). Nothing here is implemented yet. Written against the
interconnect code on `esweek-tutorial`, which is byte-identical to `stable` in this
area.

## 1. What exists today, precisely

The whole mesh/NoC path is ~1 160 lines:

| piece | what it does |
|---|---|
| `Mesh` | one node per `component_ids` entry; `connection_matrix[i]` is node *i*'s adjacency list; one `MeshInterface` per node; ticks the controller on falling edges (`m_clk_period` 50 ns, same as the bus) |
| `MeshInterface` | per node: **one TX request queue and one TX data queue per adjacent link**, plus one RX request and one RX data queue; `pushMessage` splits a multicast `to` list into one unicast copy per destination and drops each into the queue of the link that leads *directly* to it |
| `MeshController::Link` | one `Link` per adjacency pair **per class** (`RequestLink` latency 2, `DataLink` latency 5); each link has its own `Arbiter` (FCFS between the two endpoints' TX queues; RR/TDM over the global core slots) and is half-duplex: one message in flight per latency window, either direction |
| `MeshController::step` | per interconnect cycle: for every idle link, elect a message; after `latency` cycles, `send()` places it straight into the destination's RX |
| `NoC` | `Mesh` plus `Switch` nodes. A switch is just another interface. `NoC::cycleProcess` drains every switch's RX and re-pushes each message into its TX **in the same cycle, with no latency and no arbitration of its own** |
| `NoCInterface::m_routing_table` | `map<dest → next-hop link>`, built once at construction: every direct neighbour maps to itself, and for each adjacent *switch*, everything that switch lists maps to the switch |

So a message can traverse at most one switch, because a switch's own routing table only knows its direct neighbours; a destination behind two switches has no entry. That is the single-hop limit. Routing is per-source, static, and has no notion of path choice, distance, or congestion.

### Defects in the current code, worth fixing regardless of the plan

1. **Unknown destination is dropped silently.** `noc_pushMessage` does `m_routing_table[to_id]` — `operator[]` on a `std::map` inserts 0 for a missing key — finds no link with id 0, pushes nothing, and **returns true**. A misconfigured or two-hop destination vanishes without a message. It should abort with the destination named.
2. **TX queues are unbounded.** The capacity check is `m_tx_request_buffer.size() < m_buffer_max_size`, but `m_tx_request_buffer` is the vector *of per-link queues*, so `.size()` is the link count — a constant. The intended check is on the chosen queue's depth. Consequence: no back-pressure on the TX side; combined with `exit(0)` on a full RX ("full buffer"), any real congestion crashes instead of stalling.
3. **No per-hop observability.** `send()` logs a single `REQ_BUS`/`RESP_BUS` EXIT with component 0, so the LatencyReport's "bus" columns and the visualizer see one opaque hop however far the message travelled.

## 2. Target

A k-ary n-cube style network of **routers**, each with input-buffered ports, a pluggable **routing function**, per-hop latency, and credit-based back-pressure; deadlock-free by construction for the shipped algorithms; deterministic; configured from CSV like everything else; and reachable by the existing controllers through the unchanged `CommunicationInterface` contract, so no cache controller or protocol changes.

Explicitly in scope: directory protocols (point-to-point traffic). Explicitly out of scope for now: snooping on a NoC (needs an ordered broadcast tree; separate design).

## 3. Design

### 3.1 Principles

- **Controllers never change.** Endpoints keep calling `pushMessage(msg with `to`)` / `peekMessage` / `popFrontMessage`. Everything below that line is the NoC's business.
- **Topology is data, routing is a plug-in.** Topologies come from CSV (explicit matrix as today, or a generator). Routing algorithms are classes selected by name, exactly the way arbiters are.
- **Reuse what works:** `Link` for link occupancy and latency; `Arbiter` for output-port allocation; the `Logger` role/phase scheme for per-hop tracing.
- **The current single-switch NoC becomes a degenerate case** (one router, star topology, table routing) so existing configs keep running.

### 3.2 Components

```
Topology         builds the graph: routers, links, endpoint→router attachment, coordinates
RoutingAlgorithm nextHop(here, dest, msg) -> output port          [plug-in, by name]
Router           input buffers per port × class, output arbiters, credits, pipeline latency
NetworkInterface endpoint side: unchanged contract; splits multicast; injects into its router
NoCController    the per-cycle engine: route → allocate → traverse, over all routers/links
```

**`Topology`** replaces the hand-written adjacency-only view with three things the router needs: adjacency, **coordinates** (so dimension-order routing can exist), and an **endpoint map** (which cache/LLC id hangs off which router). Sources:

- `topology(s)=custom` — today's `connection_matrix`, unchanged.
- `topology(s)=mesh2d`, `dims(vi)=k,k` — generated; routers numbered row-major; `node_map[<endpoint id>](i)=<router>` attaches endpoints. `torus2d` and `ring` are one-line variants.

**`RoutingAlgorithm`** is the modular part the question is about:

```cpp
class RoutingAlgorithm {
public:
    virtual int  nextHop(int router, int dest_router, const Message &m) = 0;   // output port
    virtual bool deadlockFreeOn(const Topology &) const = 0;                    // self-check at init
};
```

Shipped implementations, in order of delivery:

| name | what | deadlock freedom |
|---|---|---|
| `Table` | static next-hop table; computed at init by BFS shortest paths on the topology, or loaded from CSV (`routing_table[<router>](vi)=dest:port,...`) | not guaranteed — the init check walks the channel-dependency graph and refuses a cyclic table unless VCs are enabled |
| `XY` | dimension-order for `mesh2d`; X then Y | free by construction |
| `WestFirst` / turn model | partially adaptive on `mesh2d` | free by construction |
| `AdaptiveMinimal` | picks among minimal outputs by credit count; needs an escape VC running XY | free given the escape channel |

The important property is that a new algorithm is **one class implementing `nextHop`** and one line in the factory. That is also a good tutorial exercise once the interface exists — the mesh analogue of "add an arbiter".

**`Router`** replaces `NoC::Switch`. Per input port: one FIFO per message class (see 3.3), depth `vc_depth`. Per output port: an `Arbiter` (the existing classes) over the input FIFOs that currently want that output, plus a **credit counter** equal to the free slots in the downstream input FIFO. A flit... a *message* (we do not model flits; one message = one transfer, as today) moves when: it is at the head of its FIFO, its output is chosen by `nextHop`, the output arbiter picks it, and the output has a credit. `router_latency` cycles later it is on the link; `Link.latency` later it is in the next router's FIFO (or the endpoint's RX). This is a conventional 2-stage (RC/SA, then LT) model, and deliberately simple.

**Back-pressure** is the credit counter, end to end: an endpoint injects only when its router's input FIFO has space; a router forwards only when the next FIFO has space. The two `exit(0)` "full buffer" paths become genuine stalls. Fix 2 above is the endpoint side of this.

### 3.3 Message classes and virtual networks

Today request and data already travel on separate `Link`s (two physical networks). Keep that separation as **virtual networks** sharing physical links, and use three classes, because a directory protocol has three message types whose cyclic dependency is the textbook protocol deadlock:

| class | messages | how to classify |
|---|---|---|
| REQUEST | GetS/GetM/PutS/PutM from L1s to the LLC | `data == NULL`, destination is the LLC |
| FORWARD | Fwd_GetS/Fwd_GetM/Inv from the LLC to L1s | `data == NULL`, source is the LLC |
| RESPONSE | data, InvAck, Put_Ack | `data != NULL`, or ack kinds |

`Message::kind` already carries the raw-trace kinds (`K_RESP`, `K_FILL`, `K_WB_INV`, …); the classifier should key on `kind` where it is set and fall back to the data/direction rule. Each class gets its own FIFO per input port. This is the same separation the earlier "forward-request virtual channel" note asked for on the bus, and it is cheaper to get right in a new NoC than to retrofit.

With XY routing and per-class virtual networks, the shipped configuration is deadlock-free without adaptive machinery. `Table` routing gets the init-time cycle check instead.

### 3.4 Timing and clocking

Unchanged framing: the NoC is a `ClockedObj` stepping on falling edges. New parameters, all per-interconnect in the CSV:

```
interconnect.topology(s),mesh2d
interconnect.dims(vi),2,2
interconnect.routing(s),XY
interconnect.router_latency(i),2       # route + allocate
interconnect.link_latency(i),1         # per hop, replaces m_request_latency / m_data_latency
interconnect.vc_depth(i),4             # per input port per class
interconnect.node_map[0](i),0          # L1 0 on router 0 ... LLC (id 10) on router 3
```

A 2×2 mesh with four L1s and one LLC then costs 1–3 hops per message instead of a flat 2 or 5, which is the first result worth showing.

### 3.5 Observability

- New `Logger::Role::NOC` with `comp = router id`, phases ENTER (into an input FIFO), SERVICE (won allocation), EXIT (onto the link). The visualizer gets one lane per router for free from the existing resource-lane code; the coherence transition table is unaffected.
- LatencyReport: the "Request Bus" and "Response Bus" columns become the *sum over hops*; a per-request hop count goes in the mechanism-tracker columns. The event-path tiling check (`[EVENT-PATH]`) must still sum to Total — this is the validation that catches an accounting mistake.
- `OCTOPUS_HANG_DUMP` extends to routers: per-port FIFO occupancy and credits. A routing deadlock then shows as a ring of full FIFOs, which is exactly what one wants to see.

### 3.6 Multicast

`to` lists (an LLC invalidating several sharers) are split into unicasts at the network interface, as now. A multicast tree is a later optimisation; correctness never depends on it.

## 4. Phasing

**Phase 0 — fixes to the existing code (hours).** Abort on unknown destination instead of dropping; fix the TX depth check; document the single-hop limit in `SUPPORTED_CONFIGURATIONS.md`. Independent of the rest and worth doing on its own.

**Phase 1 — the network (the real work; ~2 weeks for someone who knows the code).** `Topology` (custom + `mesh2d`), `Router`, credits, `NoCController` engine, `Table` and `XY` routing, two classes (request/data, as today) so the existing three-class question does not block progress. Acceptance: directory MSI EEMBC 10/10 on a 2×2 mesh, deterministic, and byte-identical results to today's single-switch NoC when configured as one router with `Table` routing.

**Phase 2 — protocol-grade (~1 week).** Three message classes; deadlock checker for `Table`; `Role::NOC` tracing and visualizer lane; LatencyReport hop accounting with the tiling check green; `HANG_DUMP` router view. Acceptance: 4×4 mesh with the four L1s and the LLC placed at the corners, EEMBC 10/10, and a worst-case-latency-vs-placement result that makes sense.

**Phase 3 — research surface.** Turn-model and adaptive routing with an escape VC; `torus2d`; multiple LLC banks on different routers (needs the L1 to pick a home by address — an open question, see below); and the real-time angle that fits the group's work: **TDM slot tables per link** (`routing(s)=TDM_XY`), which turns the NoC into a predictable one and gives the tutorial's arbiter story a network analogue.

## 5. Open questions

1. **LLC banking.** A single LLC node on a mesh is a hotspot and misrepresents real designs. Does the L1 path already support several `m_shared_memory_id`s selected by address? If not, that is the one controller-side change the plan needs, and it belongs to Phase 3.
2. **Snooping on a NoC.** Needs an ordered broadcast; not planned here.
3. **Message vs flit granularity.** The plan keeps one message per transfer. Flit-level modelling changes link occupancy and is a separate decision.
4. **Where the RT-predictable NoC work lands** — Phase 3 or a fork of Phase 1, depending on which paper it is for.

## 6. Relationship to the tutorial

None for ESWEEK — this is weeks, not days. But once Phase 1 exists, "add a routing algorithm" is a better 45-minute extension exercise than "add an arbiter": same shape (one class, one factory line), and the result is visible as a different path on the visualizer's router lanes.
