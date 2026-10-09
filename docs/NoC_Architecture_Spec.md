# Octopus Network-on-Chip — architecture specification

Status: draft 2, 2026-10-09. Draft 1 was reviewed the same day; the seventeen findings and the
review's improvements are folded in (change list in §22). Real-time arbitration (TDM slot tables,
priority VCs) is **deferred** until the infrastructure is built; §12 keeps only the hooks. Companion to
`docs/NoC_MultiHop_Plan.md` (the why and the phasing); this document is the what, at the level an
implementer codes from and a reviewer checks against. Nothing in it is implemented. Normative
statements use **shall**; everything else is explanation.

Contents

1. Scope and goals
2. Terms
3. The contract with the rest of Octopus
4. Configuration
5. Topology
6. Packets, flits and classes
7. Network interface (endpoint side)
8. Router
9. Flow control: virtual channels and credits
10. Routing
11. Arbitration and allocation
12. Real-time arbitration (deferred): the hooks kept now
13. Timing: clock, units and the per-tick algorithm
14. Ordering and deadlock freedom
15. Observability and statistics
16. Error handling and determinism
17. Compatibility and the degenerate configuration
18. Traffic generators (network-only mode)
19. Code structure
20. Validation
21. Worked example
22. Changes from draft 1
23. Open issues

---

## 1. Scope and goals

The network replaces the single-switch `NoC` with a general packet-switched network of routers:

- **Topology is data** (explicit adjacency, or a generated grid, torus or ring), **routing is a
  plug-in** selected by name, exactly as arbiters are today.
- **Flit-level wormhole switching** with virtual channels and credit-based flow control, so that
  the blocking behaviour that dominates worst-case latency is modelled, not approximated.
- **Per-class virtual networks** so that a directory protocol's three message classes cannot form
  a buffer-dependency cycle.
- **Deterministic**: the same inputs produce byte-identical reports on every platform.
- **Observable**: every hop stamped, every wait attributed, visible in the visualizer.
- **Unchanged endpoints**: cache controllers and protocols keep the `CommunicationInterface`
  contract and never learn that a network sits between them.

Out of scope for this specification: snooping over the network (needs an ordered broadcast tree),
memory controllers on the network, flit-level power. These are Phase 3 items in the plan.

## 2. Terms

| term | meaning here |
|---|---|
| endpoint | a cache controller, LLC bank or memory controller that owns a `CommunicationInterface` id |
| router | a switching node with link ports and local ports |
| port | one side of a link at a router; ports are numbered per router, local ports last |
| link | a unidirectional channel between two ports; a physical connection is two links, one per direction |
| local link | the connection between an endpoint's NI and its router's local port; zero latency |
| message | the `Message` object the endpoints exchange, unchanged |
| packet | one message in transit, as a sequence of flits with a head and a tail |
| flit | the unit of link transfer and buffer accounting; `flit_bytes` wide |
| class | request, forward or response; one virtual network each |
| VC | virtual channel: one flit FIFO at an input port, belonging to one class |
| credit | permission to send one flit into a specific downstream VC, or one packet into an RX queue |
| tick | one step of the network clock; two ticks per core cycle in the shipped configuration |
| R | the number of routers a packet traverses, including the first and the last |
| NI | network interface: the endpoint side of the network, implementing `CommunicationInterface` |

## 3. The contract with the rest of Octopus

### 3.1 What the endpoints see

The NI implements the existing interface and nothing else; its visible behaviour is defined here.

| call | NI behaviour |
|---|---|
| `pushMessage(msg, cycle, type)` | stamps `msg.from` with the endpoint id, as every Octopus interface does (the directory L1 distinguishes the home's data from an owner's by it); accepts the message if the per-class injection queue has at least `msg.to.size()` free slots; returns `false` otherwise, and the sender retries on its next tick. Shall never drop a message. The `type` argument is ignored, as the mesh does today; class comes from the message (§6.3). |
| `peekMessage(out)` / `popFrontMessage()` | serve the endpoint's per-class RX queues in the order selected by `rx_serving` (§7.4). Within a class, FIFO. |
| `pushMessage2RX(msg, type)` | used only by the network itself on ejection. Because an RX slot is reserved for a packet when its head is admitted to the last router (§8.4), the call shall always succeed; a `false` is an internal error and fatal (§16). |
| `canAcceptRX(type)` | true when the RX queue for that type's class has an unreserved free slot. |
| `txResponseFreeSlots()` | free slots in the response-class injection queue. |
| `rollback(addr, mask, out)` | unchanged semantics: searches the injection queues, as `MeshInterface::rollback` does. |

Multicast: a `to` list with several destinations is split into one packet per destination at
injection, in the order of the list, as today. The message is accepted or refused atomically for
all destinations.

### 3.2 Message fields the network reads

`msg_id`, `addr`, `owner` (the endpoint the packet belongs to: the requesting core for requests,
supplies and fills, the LLC for invalidations and forwards it originates; carried for the deferred
ownership-based policies, §12), `from`, `to`, `data`/`data_size` (payload size), `kind`, `complementary_value` and
`source` (fallback classifier, §6.3). Apart from the `from` stamp at `pushMessage`, the network
shall not modify any field of a message in transit, and shall deliver the message object
bit-identical to the one injected (`to` included: the legacy interfaces narrowed it to the one
destination, the controllers only test membership).

### 3.3 System wiring

`MultiCoreSystem_Mesh` selects the interconnect by `interconnect_type(s)`: `Mesh` and `NoC` today.
The new network is `interconnect_type(s),Network`, class `Network`, a `ClockedObj` and
`Configurable` like `Mesh`, exposing `getInterfaceFor(id)` and `getLowerLevelIds()` with the same
signatures so the system class needs no other change. The legacy `NoC` class remains selectable
until the degenerate-configuration acceptance (§17) has passed, after which `NoC` becomes an
alias of `Network` with the legacy keys mapped (§17.2).

Two values the network needs from the system level: the home ids (§4.1, `home_ids`, defaulting to
the set of `cache_controller[*].m_shared_memory_id` values) and the protocol's ordering
requirement (`requires_ordering`, a system-level key set by the protocol presets, §14.1).

## 4. Configuration

All keys live under the interconnect's CSV (`configuration/Interconnect/Network.csv`, extending
`Mesh`), overridable per instance and from the command line as every other key. Types follow the
existing convention: `(i)` int, `(s)` string, `(vi)` int list, `(vs)` string list.

### 4.1 Topology

| key | type | default | meaning |
|---|---|---|---|
| `topology` | s | `custom` | `custom`, `grid2d`, `torus2d`, `ring`, `star` |
| `component_ids` | vi | — | endpoint ids (unchanged) |
| `connection_matrix[i]` | vi | — | `custom` only: node id then its neighbours (unchanged); nodes that are not in `component_ids` are routers |
| `dims` | vi | — | `grid2d`/`torus2d`: `k_x,k_y`; `ring`: `k` |
| `node_map[<endpoint id>]` | i | — | generated topologies: the router an endpoint attaches to; every endpoint shall be mapped |
| `router_ids` | vi | derived | `custom` only: which ids in the matrix are routers; defaults to the ids absent from `component_ids` |
| `home_ids` | vi | derived | ids of the LLC / directory homes, for the classifier (§6.3); defaults to the system's `m_shared_memory_id` values |
| `local_ports` | i | 0 | local ports per router; `0` means one per attached endpoint (no sharing). A positive value smaller than the endpoints at a router makes endpoints share ports (§5.2) |

### 4.2 Switching and flow control

| key | type | default | meaning | constraint |
|---|---|---|---|---|
| `flit_bytes` | i | 16 | link width; `0` means one flit per packet (§6.2) | ≥ 0 |
| `header_bytes` | i | 8 | bytes charged for a packet's header | ≥ 0 |
| `num_classes` | i | 3 | virtual networks (§6.3); `2` selects the legacy request/data split | 2 or 3 |
| `vcs_per_class` | i | 1 | VCs per class per input port | ≥ 1 |
| `vc_depth` | i | 4 | flits per VC | ≥ 1 |
| `router_latency` | i | 2 | ticks from a head's arrival at an input VC to its earliest switch traversal | ≥ 0 |
| `link_latency` | i | 1 | ticks per link traversal between routers; local links cost 0 | ≥ 1 |
| `class_link_latency` | vi | — | optional per-class link latency, index = class; overrides `link_latency`; used by the degenerate mapping (§17) and available generally | each ≥ 1 |
| `credit_latency` | i | = `link_latency` | ticks for a credit to return | ≥ 0 |
| `half_duplex` | i | 0 | `1`: the two links of a physical connection share one occupancy, so only one flit is in flight on the connection in either direction (legacy behaviour, §17) | 0 or 1 |
| `link_model` | s | `pipelined` | `pipelined`: a link accepts one flit per tick and delivers it `link_latency` ticks later (a wire pipeline); `serial`: a flit holds the link for its whole latency and is at the far side `link_latency − 1` ticks after it was sent (the transfer counts the sending tick, as the legacy `MeshController::Link` does), so a link carries one flit per `link_latency` ticks and a message crosses a direct link in `link_latency − 1` ticks (the legacy transfer model, §17; `link_latency ≥ 2`) | |
| `inject_queue_depth` | i | 16 | per class, packets, at the NI | ≥ 1 |
| `eject_queue_depth` | i | 16 | per class, messages, at the NI (the endpoint's RX) | ≥ 1 |
| `rx_serving` | s | `priority` | `priority`: response, forward, request; `alternate`: the legacy round-robin between RX queues (§7.4) | |

### 4.3 Routing and arbitration

| key | type | default | meaning |
|---|---|---|---|
| `routing` | s | `XY` for grids, `Table` otherwise | `Table`, `XY`, `WestFirst`, `AdaptiveMinimal` |
| `routing_table[<router>]` | vi | computed | `Table`: `dest,port,dest,port,...`; absent entries computed by BFS |
| `escape_vc` | i | -1 | VC index reserved for the escape routing function under `AdaptiveMinimal`; -1 = none |
| `arbiter_type` | s | `FCFSArbiter` | policy for VC and switch allocation: `FCFSArbiter`, `RRArbiter` (real-time policies deferred, §12) |

`requires_ordering(s)` is a system-level key (§3.3), not an interconnect key.

### 4.4 Legacy keys (accepted, mapped)

`m_request_latency`, `m_data_latency`, `buffers_max_size`, `switch_ids`: see §17.2.

### 4.5 Validation at construction

The network shall refuse to start, with a message naming the key, when: an endpoint has no router;
a `custom` matrix is not symmetric; any constraint in §4.2 is violated; `escape_vc` ≥
`vcs_per_class`; a `routing_table` names a port the router does not have; `requires_ordering` demands what `routing` and `vcs_per_class` cannot provide (§14.1); `torus2d`
is combined with `XY` and `vcs_per_class = 1` (§10.2); or the `Table` deadlock check fails (§14.3).

## 5. Topology

### 5.1 Model

A topology is a directed graph of routers plus an attachment of endpoints. Each router has
`P = links + local_ports` ports, numbered 0..P-1 with the link ports first in a fixed order and the
local ports last. Each link between routers is unidirectional and has an owner router and port at
each end. Every physical connection in a `custom` matrix becomes two links; with `half_duplex = 1`
the two links share one occupancy.

Coordinates: `grid2d` and `torus2d` number routers row-major, `r = y·k_x + x`; ports are ordered
`east, west, north, south, local…` with absent directions skipped at edges. `ring` orders
`clockwise, counter-clockwise, local…`. `custom` orders ports by the neighbour's position in the
node's matrix row; every endpoint node of a matrix gets an **implicit router** of its own id,
which stands for the endpoint's link ports and adds no `router_latency` (only explicit routers,
the matrix nodes that are not endpoints, are pipeline stages). `star` is one router whose ports
are all local.

### 5.2 Endpoint attachment

Each endpoint is attached to one router through one local port over a zero-latency local link.
With `local_ports = 0` every endpoint has its own local port. With a positive `local_ports`
smaller than the endpoints at a router, endpoints are assigned round-robin to the ports and
share them: on the injection side the port's input VCs are shared and the NIs arbitrate for them
with the configured `arbiter_type`; on the ejection side the port's output credits are kept per
(endpoint, class) and the router demultiplexes by the packet's destination id. Sharing is a
modelling option, never a default.

### 5.3 Construction output

The topology object exposes: `routers()`, `ports(r)`, `link(r, p) → (r', p')`, `coord(r)`,
`routerOf(endpoint)`, `localPort(endpoint)`, `endpointsAt(r)`, `endpointsAtPort(r, p)`. Routing
functions and the deadlock checker use only this interface.

## 6. Packets, flits and classes

### 6.1 Packet

A packet carries exactly one `Message` for exactly one destination, plus network bookkeeping:

```
Packet { Message msg;  uint64_t seq;  int cls;  int src_router, dst_router;  int flits;
         uint64_t t_inject;  per-hop stats (§15.3);  }
```

`seq` is a network-global injection sequence number, unique, assigned in injection order
(packets of one multicast in `to` order); it is the tie-breaker everywhere (§16). Only the head
flit refers to the packet; body and tail flits carry the packet's `seq` and their index.

### 6.2 Flit count

```
payload   = (msg.data != NULL) ? msg.data_size : 0
flits     = (flit_bytes == 0) ? 1 : ceil((header_bytes + payload) / flit_bytes)
```

With the defaults a request is 1 flit and a 64-byte data message is 5 flits. A packet of one flit
is both head and tail.

### 6.3 Classes

`num_classes = 3` (default):

| class | index | members | classifier |
|---|---|---|---|
| REQUEST | 0 | GetS, GetM, PutS, PutM | `kind` ∈ {K_GETS, K_GETM, K_PUTS, K_PUTM, K_DEMAND}; fallback: `data == NULL` and the destination is in `home_ids` |
| FORWARD | 1 | FwdGetS, FwdGetM, Inv, and a home's PutAck | `kind` ∈ {K_FWD, K_INV}, or K_ACK without data from a home; fallback: `data == NULL` and the source is in `home_ids` |
| RESPONSE | 2 | data, InvAck, fills, supplies | `kind` ∈ {K_RESP, K_SUPPLY, K_SUPPLY_DEFERRED, K_FILL, K_FILL_ROLLBACK, K_WB_DATA, K_MEM_READ, K_MEM_WRITE, K_EVICT}, K_ACK otherwise; fallback: `data != NULL` |

A home's PutAck rides the FORWARD class deliberately. The shipped directory L1 FSM handles a
home's Inv or Fwd followed by its PutAck to the same cache (SI_a, MI_a, II_a) but not the
reverse order: a PutAck overtaking an Inv leaves the cache in I when the Inv arrives, which the
FSM marks as a fault, and a PutAck overtaking a FwdGetM would lose the forwarded request. The
legacy mesh delivered both on its request link in FIFO order; keeping them in one class with
`pair_per_class` ordering preserves that guarantee. It costs no deadlock freedom: an ack is
consumed without injecting anything, so the forward class still drains independently of the
request class (found by S5-EEMBC-2x2, see the implementation report).

That is not the only order the shipped directory protocol assumes. The LLC FSM has no row for a
PutS or PutM arriving after the line was evicted (state N): it relies on a cache's PutS reaching
the home before the cache's InvAck to the home's eviction Inv, which holds only when the two
travel in one FIFO class, and three classes put them in REQUEST and RESPONSE. The legacy mesh
carried every non-data message of a pair on one request link in order, and the FSMs were written
against that. Until the FSMs are made tolerant of an unordered network (the plan's
P2-THREE-CLASS: `(I, Inv)` → InvAck, `(N, PutS/PutM)` → PutAck, and the symmetric races), the
protocol presets (`MultiCoreSystem_Grid.csv`) set `num_classes = 2`, which is the legacy split
and reproduces the legacy ordering under `pair_per_class` routing. `num_classes = 3` remains the
default for the network itself and is what synthetic traffic and the NetworkOnly system use.

`num_classes = 2` is the legacy split and uses exactly the legacy rule, `data == NULL ? 0 : 1`,
ignoring `kind`, so that the degenerate configuration (§17) classifies as the old mesh did. The
classifier shall log, once per run, the count of packets classified by the fallback rules, so
that an untagged protocol is visible. A message that matches no rule shall abort with its fields
printed (§16).

## 7. Network interface

Per endpoint, the NI holds `num_classes` injection queues of packets and `num_classes` ejection
queues of messages (the endpoint's RX), each bounded by the depth keys.

### 7.1 The NI as an upstream port

For injection the NI is the upstream side of its router's local input port: it keeps, per class
and VC of that input port, a credit counter (initialised to `vc_depth`) and the identity of the
packet currently holding the VC, exactly as a router keeps them for its output VCs (§8.1). Credit
returns from the router reach the NI after `credit_latency` ticks like any other credit. With a
shared local port (§5.2) these counters belong to the port and the sharing NIs arbitrate for them.

### 7.2 Injection

Each tick, for each class in order RESPONSE, FORWARD, REQUEST, if the head packet of the class's
injection queue has not started, a VC of its class on the local input port is free, and that VC
has a credit, the NI sends the head flit; subsequent flits follow one per tick while credits allow.
At most one flit per tick per local port. A packet, once started, is not interleaved with another
packet on the same VC (wormhole). Local links have zero latency: a flit sent in the injection
phase is in the router's VC in the same tick (§13.2).

VC choice at injection: the lowest-index free VC of the class whose `vc_mask` from the route
admits it. If none is free the packet waits.

### 7.3 Ejection

A packet's RX slot is reserved when its head is admitted into the last router's local output port
(§8.4), so when the tail flit reaches the local port the packet is complete and
`pushMessage2RX(msg, type)` shall succeed. The message is delivered whole: the last router holds
the `Message` from the head's arrival until the tail's ejection, which is the one store-and-forward
step in the network and is inherent to delivering message objects.

### 7.4 RX serving order

`rx_serving = priority`: `peekMessage` returns the front of the RESPONSE queue if non-empty, else
FORWARD, else REQUEST. `rx_serving = alternate`: the legacy `MeshInterface` behaviour, a selector
that alternates between the queues that are non-empty, applied to the class queues in index
order; the selector advances on every `peekMessage`, not on the pop, so a peek that is not
followed by a pop (a full processing queue) serves the other class next time, exactly as the
legacy interface does. The degenerate configuration uses `alternate` with two classes, which reproduces the legacy
request/data alternation exactly.

### 7.5 Stamps at the NI

The NI records `REQ_BUS`/`RESP_BUS` **EXIT** through `Logger::event` at ejection, with
`comp_id = 0`, exactly where `MeshController::send` records it today, so the LatencyReport's bus
columns keep their definition and the tiling check keeps passing. Injection and ejection are also
written to the raw trace (`Logger::trace`) as ENTER and EXIT of the same roles, as the mesh does
through `traceLink`; the ENTER goes to the trace only and never through `event`, so the report path
sees no stamp it was not written for. The per-hop detail goes through the `NOC` role (§15).

## 8. Router

### 8.1 State

Per router: `P` input ports; per input port, `num_classes × vcs_per_class` VCs; per VC: a flit FIFO
of `vc_depth`, the packet whose flits are at the FIFO front (the **current packet**), its route
state (output port and output VC once allocated), and the arrival tick of its head. A VC's FIFO may
contain the tail of one packet followed by the head of the next, because the upstream router
releases its output VC when the tail is *sent*, not when it is consumed downstream; the current
packet is always the one at the front, and the next packet's head is not routed until it reaches
the front.

Per output port: per output VC a credit counter (initialised to the downstream `vc_depth`, or for a
local port the RX reservation counter of §8.4), the packet holding the output VC, and a
`busy_until` tick for the link (shared between the two directions when `half_duplex = 1`); per
output port a switch-allocation arbiter; per output VC an allocation arbiter.

### 8.2 Pipeline

Per packet: **RC** route computation when the head is at the FIFO front, **VA** virtual-channel
allocation (claims one free output VC of the packet's class on the computed output port, within
the route's `vc_mask`, §10.1), then per flit **SA** switch allocation (wins the output port for this
tick) and **LT** link traversal. RC and VA happen once per packet; SA and LT once per flit. A
packet's head may begin SA no earlier than `router_latency` ticks after its arrival at the input
VC. Body and tail flits of a packet that has won VA are eligible for SA from the tick they arrive,
subject to credits and the switch arbiter, and leave in FIFO order behind the head. Cut-through
follows: a head leaves while its tail is still upstream.

### 8.3 Output VC release

The output VC claimed at VA is released when the tail flit has been sent on the link. Credits are
per output VC and are consumed one per flit sent; a credit returns `credit_latency` ticks after the
downstream router pops the corresponding flit from its FIFO.

### 8.4 Local (ejection) ports

A local output port's "downstream" is the NI's class RX queue. Its credit is counted in packets: one
credit per unreserved free RX slot of that (endpoint, class). VA on a local port consumes one credit
for the whole packet, which is the RX reservation of §7.3; the slot is filled when the tail is
ejected. With a shared local port (§5.2) the credit counters are per (endpoint, class) and VA
selects the counter by the packet's destination id. A local port has no VA competition between
classes because classes map to distinct RX queues.

## 9. Flow control

- A flit is sent on a link only when the destination VC holds a credit for it.
- Credits are returned on the pop of a flit from the downstream FIFO, not on its arrival, and reach
  the upstream router after `credit_latency` ticks.
- `vc_depth` may be smaller than a packet's flit count; that is the wormhole case and is allowed.
  `vc_depth ≥ flits` for every class is virtual cut-through; `flit_bytes = 0` and `vc_depth ≥ 1`
  is the message-level model.
- Per-VC throughput is bounded by `vc_depth / (link_latency + credit_latency)` flits per tick: a
  flit sent at tick `t` arrives at `t + link_latency`, is forwarded in that same tick when the
  downstream VC already holds the packet's route, and its credit is back and usable at
  `t + link_latency + credit_latency` (§13.2 applies arrivals before switch allocation). The head of
  a packet additionally waits `router_latency` downstream, so the bound is reached by body flits
  and, with `router_latency = 0`, by every flit; the manual states this so users can size `vc_depth`
  deliberately (validated by S4-CREDIT-TPUT).
- Conservation: at the end of a run, flits injected equal flits ejected plus flits in flight, and
  no credit counter ever exceeds its initial value. Both are asserted in debug builds and checked
  by the test suite (§20).

## 10. Routing

### 10.1 Interface

```cpp
struct RouteContext { int router; int in_port; int in_vc; int dst_router; int cls; const Packet &p;
                      const Router &here; };            // `here` exposes credit counts only
struct Route        { int out_port; uint32_t vc_mask; }; // bit i set = output VC i of the class allowed
class RoutingAlgorithm {
public:
    virtual ~RoutingAlgorithm() {}
    virtual Route    nextHop(const RouteContext &) const = 0;
    virtual Ordering ordering() const = 0;                          // NONE or PAIR_PER_CLASS
    virtual bool     needsEscapeVC() const { return false; }
    virtual bool     deadlockFreeOn(const Topology &) const = 0;    // for Table: run the checker
    virtual std::string name() const = 0;
};
```

Registration: a factory keyed by name, one line per algorithm, the same pattern as
`SplitBusController`'s arbiter factory. `nextHop` shall be a pure function of its arguments and the
immutable topology; the only mutable state it may read is the current router's credit counts
through `here`, so that determinism holds (§16). `vc_mask` is how a routing function constrains
the VC (dateline rules, escape channels); `~0` means any VC of the class.

### 10.2 Shipped algorithms

| name | topologies | definition | ordering |
|---|---|---|---|
| `Table` | any | static next hop per (router, dest); computed by BFS shortest path at init with ties broken by lowest port index, or loaded from `routing_table`; `vc_mask = ~0` | PAIR_PER_CLASS with `vcs_per_class = 1` |
| `XY` | grid2d, torus2d | route along X to the destination column, then along Y; at the destination router, the local port. On `torus2d` the shorter direction around each ring is taken and the **dateline rule** applies: a packet that has crossed, or will cross, the wrap link of the current dimension gets `vc_mask = 0b10` (VC 1), others `0b01`; construction refuses `torus2d` + `XY` with one VC | PAIR_PER_CLASS with `vcs_per_class = 1` on grid2d; on torus2d the dateline keeps a pair's packets on one VC, so PAIR_PER_CLASS holds with 2 VCs |
| `WestFirst` | grid2d | turn model: a packet with a westward component goes west first; otherwise, among the minimal outputs allowed by the turn rules, the one whose output VCs of the class hold the most credits, ties by lowest port index; adaptive | NONE |
| `AdaptiveMinimal` | grid2d, torus2d | among minimal outputs, the one with the most credits, ties by lowest port index, `vc_mask` = all VCs except `escape_vc`; a packet that finds no credit on any adaptive VC is routed by `XY` with `vc_mask = 1 << escape_vc`, and a packet that arrives over a link on the escape VC stays on it (the VC a packet was injected on at its local port carries no meaning) | NONE |

### 10.3 Multicast

Not routed. One packet per destination (§3.1).

## 11. Arbitration and allocation

### 11.1 Candidate model

Both allocators present their candidates to an arbiter as a vector of

```cpp
struct Candidate { int index; int owner; uint64_t seq; };   // owner = msg.owner (an endpoint id)
```

and the arbiter returns the winning `index`. A new virtual on `Arbiter`,
`int electIndex(uint64_t tick, const std::vector<Candidate> &)`, is added with implementations:

| arbiter | rule |
|---|---|
| `FCFSArbiter` | lowest `seq` |
| `RRArbiter` | rotate a pointer over endpoint ids; the first id in rotation order that has a candidate wins with its lowest-`seq` candidate; the pointer advances past the winner |

`TDMArbiter` and a `PriorityArbiter` get their `electIndex` in the deferred real-time phase (§12);
the `owner` field is carried in every `Candidate` from day one so that they need no change to the
allocators when they arrive.

The existing `elect(cycle, buffers, out_msg)` stays for buses; the two entry points shall share the
rotation state so that an RR arbiter used on a link and on a router behaves the same way.

### 11.2 VC allocation

Per output VC, per tick: candidates are the input VCs whose current packet has computed this output
port, whose class matches, whose `vc_mask` admits this VC, that hold no output VC yet, and whose
head is at least `router_latency` ticks old. One winner per output VC per tick. A free output VC is
one with no packet holding it; on a local port, VA additionally requires one packet credit for the
destination (§8.4) and consumes it.

### 11.3 Switch allocation

Per output port, per tick: candidates are the input VCs holding an output VC on this port whose
front flit is present and whose output VC has a credit (link ports) or whose reservation is held
(local ports); the link must be free (`busy_until < tick`, and under `half_duplex` the opposite
direction's link too). One winner per output port per tick; the winning flit is sent and the link
is busy for one tick (`link_model = pipelined`) or for its latency (`serial`, §4.2). Per input port,
at most one flit leaves per tick (one crossbar input). Under `half_duplex` the two directions of a
connection are one arbitration unit: the candidates of both routers' output ports compete in one
election (by age, then by the lower router index), as the legacy link arbitrated between its two
endpoints; otherwise each output port is its own unit.

## 12. Real-time arbitration (deferred)

TDM slot tables per link, a priority-preemptive VC allocator and the `TDM_XY` routing are out of
scope until the infrastructure of this document is built and validated. They are specified in the
plan's Phase 3 and will get their own section here then. What this draft keeps, because it costs
nothing now and avoids rework later:

- Every `Candidate` carries `owner` (an endpoint id, §11.1), so an arbiter that elects by owner
  needs no change to the allocators.
- `Route.vc_mask` (§10.1) lets a routing or priority rule pin a packet to a VC.
- `msg.owner` for LLC-originated packets is the LLC, so ownership-based policies will have to
  include the homes among the owners; the decision of whether the LLC competes for slots with the
  cores, or has its own, is deliberately left to the real-time phase.
- The slot-length question (a slot must be at least `link_latency` ticks) and the single-latency
  requirement for slot tables are recorded here so they are not rediscovered.

Keys `slot_table[...]`, `vc_priority[...]`, routing `TDM_XY` and arbiter `PriorityArbiter` are
reserved names and shall be refused at construction with "deferred" until implemented.

## 13. Timing

### 13.1 Clock

`Network` is a `ClockedObj` with `m_clk_period` from its CSV, 50 as shipped, stepping its controller
on falling edges exactly as `Mesh::cycleProcess` does: one network tick per two edges, that is one
tick per 100 time units, which is one core cycle in the shipped configuration (the cores run at
period 100). All latency keys are in ticks. The manual states next to the keys that one tick is
one core cycle as shipped, and that halving `m_clk_period` gives two ticks per core cycle
(measured in step 4: a 4×4 run of 21 476 ticks ended at simulation time 2 147 600).

### 13.2 Per-tick algorithm

The controller executes the following phases in order, over all routers in ascending id, all ports
in ascending index and all VCs in ascending (class, vc); decisions within a phase use the state as
it was at the start of the phase (compute, then apply), so results do not depend on iteration
order:

1. **Injection** from the NIs (§7.2), using the credits visible at the start of the tick. A flit
   injected now is in its router's VC for the rest of this tick.
2. **Arrivals.** Flits whose link arrival tick is now are appended to their destination VC FIFO.
   Credits whose return tick is now increment their counters.
3. **Route computation.** Every current packet whose head is at the FIFO front and has no route
   gets `nextHop`.
4. **VC allocation** (§11.2).
5. **Switch allocation and link traversal** (§11.3). Flits popped here schedule a credit return to
   the upstream side at `tick + credit_latency`.
6. **Ejection.** Tails that arrived at a local port complete their packet; `pushMessage2RX` is
   called and shall succeed (§7.3).

Unloaded latency of a packet of `f` flits traversing `R` routers (so `R − 1` links) is therefore

```
R · router_latency + (R − 1) · link_latency + (f − 1)   ticks
```

from the head's injection to the tail's ejection, with local links costing nothing and provided
the credits never run out on the way, which for `link_latency > 1` needs
`vc_depth ≥ min(f, link_latency + credit_latency)`; §20 asserts exactly that. With `router_latency = 0`, `link_latency = 1` and one flit, a packet crosses one
router per tick.

## 14. Ordering and deadlock freedom

### 14.1 Ordering

`Ordering::PAIR_PER_CLASS` means: two packets of the same class from the same source endpoint to the
same destination endpoint are ejected in injection order. It holds when the path and the VC are a
deterministic function of (source, destination, class), because every VC is a FIFO: that is
`Table` and `XY` with `vcs_per_class = 1`, and `XY` on `torus2d` with the dateline rule. It does
not hold across classes, across adaptively chosen VCs, or under adaptive routing. Protocol presets
declare `requires_ordering`; the directory presets declare `pair_per_class`. Construction shall
refuse a combination where the routing algorithm's `ordering()` is weaker than required, or where
`vcs_per_class > 1` without the dateline rule fixing the VC.

### 14.2 The endpoint admission property

The network guarantees freedom from buffer deadlock only if every endpoint eventually consumes any
RESPONSE or FORWARD packet delivered to its RX regardless of whether it can inject or admit
REQUESTs. Octopus controllers satisfy this today: responses and forwards are force-admitted into
the processing queue. This specification makes it a named property, `ADMIT-RF`, documented in the
controller chapter of the manual and guarded by a unit test (§20, item 5) that constructs a
controller with a full request queue and checks that a forward and a response are still consumed.
`rx_serving = priority` is what lets the NI honour it when all three RX queues are non-empty.

### 14.3 Deadlock checks

- **Routing-level** (`Table` only; `XY` and `WestFirst` are free by construction and
  `AdaptiveMinimal` by its escape VC): build the channel dependency graph whose nodes are (link, VC)
  and whose edges are the transitions the table can produce for any destination; a cycle rejects
  the table unless `escape_vc` is set, in which case the escape VC's own graph must be acyclic.
- **Protocol-level** (Phase 2): read the protocol FSM CSVs in use, enumerate the message types the
  actions can emit, classify each (§6.3), and verify that every class a controller may need to
  emit while holding a REQUEST is strictly higher in the consumption order RESPONSE > FORWARD >
  REQUEST. A protocol that emits a REQUEST while blocked on a FORWARD fails the check with the
  offending table cell named. The check runs at construction when both artefacts are present and
  its verdict is printed once. Its input, an action-to-message-class mapping, does not exist in
  data today (§23, issue 4); producing it is scheduled in Phase 1 so the check can be built in
  Phase 2 without touching the protocols twice.

## 15. Observability and statistics

### 15.1 Raw trace

A new `Logger::Role::NOC`, appended to the enum (index 12), with `comp_id = router id`:

| phase | when | flags (u16) |
|---|---|---|
| ENTER | head flit appended to an input VC | `in_port << 8 \| in_vc` |
| SERVICE | head wins VC allocation | `out_port << 8 \| out_vc` |
| EXIT | tail flit sent on the output link | `out_port << 8 \| flits` |

`tools/octoviz/tracefile.py` adds `"NOC"` to `RESOURCE`; `occupancy.py` builds one lane per router
from these records; the viewer draws the selected request's route as the ordered list of routers it
entered, and a contention view that counts, per router and port, the ticks a flit was ready but
not allocated.

### 15.2 Report stamps

Unchanged bus stamps at the NI (§7.5). No new `event()` roles, so the LatencyReport columns and the
`[EVENT-PATH]` tiling are untouched.

### 15.3 Trackers

Four annotations, written at ejection from the packet's per-hop stats and **accumulated** over the
request's packets (its request and its response), through a new `Logger::accumulate(msg_id, Annot,
delta)` beside the existing set-valued `annotate`:

| column | meaning |
|---|---|
| `NoC Hops` | routers traversed, summed over the request's packets |
| `NoC Wait` | ticks the head spent waiting for VA plus ticks any flit waited for SA or a credit |
| `NoC Transfer` | ticks on links plus `router_latency` per router plus `flits − 1` per packet |
| `NoC Ahead` | flits of other packets that won SA at a port while this packet's flit was a losing candidate; the report's worst-case footer also records the owning endpoint that won most often |

`NoC Wait + NoC Transfer` equals the packet's injection-to-ejection time; a test asserts it.

### 15.4 Network statistics file

At the end of a run the network writes `newLogger/NetworkStats.csv` with: per link, flits carried
and ticks busy; per router and output port, allocation attempts and losses (the contention count);
per class, packets, mean and maximum injection-to-ejection latency and maximum `NoC Wait`; and the
conservation totals of §9. The network-only harness (§18) reads this file to plot latency versus
injection rate; the sweep harness can read it like any other report.

### 15.5 Hang dump

`OCTOPUS_HANG_DUMP` prints, per router, per port, per VC: the current packet's `seq`, `msg_id`,
class, owner, flits present, output port and VC, and credits available on its output VC. A routing deadlock reads as a ring of held VCs with zero credits.

## 16. Error handling and determinism

- Every fatal condition calls a single `fatal(const string &)` that prints `Network: ` plus the
  message with router, port, packet `seq` and `msg_id` where applicable, flushes the hang dump if
  enabled, and exits with status 2. No path exits with status 0 on failure.
- A message with an unknown destination, an unclassifiable message, a routing function returning a
  port the router does not have or an empty `vc_mask`, a credit counter exceeding its initial value,
  or a `pushMessage2RX` that returns `false` are fatal.
- Determinism: no decision may depend on pointer values or on unordered-container iteration.
  Candidates are ordered by `(in_port, cls, vc)`; ties everywhere by `seq`. Adaptive choices use
  credit counts and lowest port index, never a random number.
- Two runs with the same configuration and traces shall produce byte-identical `LatencyReport`,
  `Summary`, `NetworkStats` and raw-trace files; this is asserted in the test suite (§20, item 7).

## 17. Compatibility and the degenerate configuration

### 17.1 The legacy single-switch NoC as a configuration

The legacy `NoC` is a set of point-to-point links (every matrix connection: one request link
and one data link, each shared by its two directions, each holding a message for its latency and
delivering it `latency − 1` cycles after election) plus switch nodes that store a message and
forward it one cycle later. Its configuration in the network (`MultiCoreSystem_NetNoC.csv`):
`topology(s),custom` with the legacy `connection_matrix` rows and `router_ids` = the legacy
`switch_ids` (endpoint nodes get implicit, zero-latency routers, §5.1; the switches are explicit
routers), `routing(s),Table`, `flit_bytes(i),0` (one flit per message), `router_latency(i),1`
(the switch's store-and-forward cycle), `vcs_per_class(i),1`, `vc_depth(i),<buffers_max_size>`
(the legacy RX depth, in messages), `num_classes(i),2`, `half_duplex(i),1`,
`rx_serving(s),alternate`, `local_ports(i),0`, `link_model(s),serial`,
`class_link_latency(vi),<m_request_latency>,<m_data_latency>`, and a large
`inject_queue_depth` (the legacy TX buffers were effectively unbounded). The star of draft 2 is
not usable here: local links cost nothing, so it cannot carry the legacy link latencies.

Acceptance: `MultiCoreSystem_Mesh` on directory MSI over EEMBC produces byte-identical
`LatencyReport` and `Summary` files to the legacy `NoC`. Known residual differences to watch: the
legacy node injects on every one of its links in the same cycle, while an NI injects one flit per
tick on its local port and its implicit router lets one flit per tick leave that input port, so a
node that sends several messages in one cycle (the LLC's invalidations and data) serialises them
over consecutive ticks; a message to a busy link also blocks the messages behind it in the local
input VC (one VC per class), where the legacy kept one queue per link. If the files are not
identical, the fallback acceptance is identical per-core worst cases and totals within one tick,
documented in the manual, and the decision to accept it is the user's. The legacy class is removed
only after acceptance.

### 17.2 Legacy key mapping

| legacy | mapped to |
|---|---|
| `buffers_max_size` | `inject_queue_depth` and `eject_queue_depth` |
| `m_request_latency` / `m_data_latency` | `class_link_latency` entries 0 and 1 when `num_classes = 2`; elsewhere ignored with a warning that `link_latency` and `flit_bytes` are in effect |
| `switch_ids` | routers in a `custom` matrix |

## 18. Traffic generators (network-only mode)

For validation and for network studies without caches, a `TrafficGenerator` endpoint implements
`CommunicationInterface` on the NI side and nothing else. Configuration under
`generator[<endpoint id>]`:

| key | type | meaning |
|---|---|---|
| `pattern` | s | `uniform`, `hotspot`, `transpose`, `neighbour`, `trace` |
| `rate` | i | injection rate in packets per 1000 ticks |
| `payload_bytes` | i | payload size, so flit count follows §6.2 |
| `cls` | i | class to inject on |
| `hotspot_id` | i | destination for `hotspot` |
| `packets` | i | number to inject, `-1` = until the run's cycle limit |
| `seed` | i | the generator's own deterministic PRNG seed |

Generators consume what they receive immediately (they satisfy `ADMIT-RF` trivially) and record
per-packet latency into `NetworkStats.csv`. A system preset `NetworkOnly.csv` instantiates
generators on every local port and no caches.

## 19. Code structure

```
header/Interconnect/Network/
  Topology.h        Topology, generators, port numbering
  Packet.h          Packet, Flit, class ids
  Classifier.h      message -> class
  Routing.h         RoutingAlgorithm, RouteContext, Route, factory
  Router.h          Router: VCs, allocators, credits, local-port reservations
  NetworkInterface.h
  TrafficGenerator.h
  Network.h         ClockedObj + Configurable, owns everything, getInterfaceFor
  NetworkController.h  the per-tick engine (§13.2)
  DeadlockCheck.h   channel-dependency and protocol-level checks
  NetworkStats.h
src/Interconnect/Network/*.cpp   one per header, plus Routing_XY.cpp, Routing_Table.cpp, ...
header/Arbiters/Arbiter.h        + electIndex and Candidate
header/Logger.h                  + Role::NOC, Annot NOC_*, accumulate()
configuration/Interconnect/Network.csv
configuration/SystemConfigurations/MultiCoreSystem_Grid.csv   2x2 grid, directory MSI, LLC on router 3
configuration/SystemConfigurations/NetworkOnly.csv
test/network/                   §20
```

The legacy `Mesh`, `MeshController`, `MeshInterface`, `NoC*` files are untouched by Phase 1 and
deleted after §17.1 passes.

## 20. Validation

The full test plan, one test id per normative sentence with its expected value, is
`docs/NoC_Test_Plan.md`; this section is the summary. Each item is a test under `test/network/`
runnable by one script, in this order:

1. **Conservation.** After every test below: flits injected equal flits ejected plus in flight;
   no credit counter above its initial value; every reserved RX slot filled or still held.
2. **Unloaded latency.** For each shipped topology and routing, one packet of 1 and of 5 flits:
   measured ticks equal `R·router_latency + (R − 1)·link_latency + (f − 1)`.
3. **Network-only harness.** `NetworkOnly.csv` on a 4×4 `grid2d`, `XY`, `flit_bytes` 16, 5-flit
   packets, `vc_depth` 4, uniform random: the mean latency versus injection rate curve saturates
   within 10 percent of the injection rate at which BookSim's equivalent configuration saturates
   (the BookSim config file is checked in beside the test). Hotspot and transpose produce the
   expected ordering of saturation points.
4. **Degenerate equivalence** (§17.1).
5. **Deadlock and admission.** A `routing_table` with a deliberate cycle is refused at
   construction; a two-class configuration with the directory protocol is refused by the
   protocol-level check; the `ADMIT-RF` unit test passes on every shipped controller.
6. **Ordering.** Under every shipped algorithm, 1000 same-class packets per pair under random
   contention arrive in order when `ordering()` is PAIR_PER_CLASS; the test is marked
   expected-fail for algorithms declaring NONE.
7. **Determinism.** Every test above run twice yields byte-identical outputs.
8. **Protocol runs.** Directory MSI, EEMBC 10/10, on `MultiCoreSystem_Grid` (2×2) and on a 4×4 grid
   with L1s and LLC banks at the corners; `[EVENT-PATH]` tiling 100 percent; `NoC Wait + NoC
   Transfer` equals the network time for every request.

## 21. Worked example

2×2 `grid2d`, routers 0..3 row-major: 0 = (0,0), 1 = (1,0), 2 = (0,1), 3 = (1,1). L1 0..3 on
routers 0..3, one each; LLC id 10 on router 3 with L1 3, each on its own local port
(`local_ports = 0`). Defaults: `flit_bytes` 16, `router_latency` 2, `link_latency` 1, `XY`.

L1 0 issues a GetS. Injection at router 0, class REQUEST, 1 flit. `XY`: X first, 0 → 1, then Y,
1 → 3, then the LLC's local port. Three routers, two links, one flit: `3·2 + 2·1 + 0 = 8 ticks` =
4 core cycles from injection to ejection into the LLC's request RX. The data response, 72 bytes,
5 flits, does not retrace that path: `XY` from (1,1) to (0,0) goes along X first, 3 → 2, then
along Y, 2 → 0, then local: `3·2 + 2·1 + 4 = 12 ticks` = 6 core cycles. Request and response use
disjoint links, which matters for contention. Under the legacy single-switch NoC the same pair
costs `2 + 5 = 7` ticks whatever the placement; on the grid, an LLC on router 0 would cost
`1·2 + 0 + 0 = 2` and `1·2 + 0 + 4 = 6` ticks, and an LLC on router 3 costs the figures above.
That placement sensitivity is the first result the plan asks for.

If L1 1 is sending a 5-flit write-back along 1 → 3 at the same time, the GetS's head arriving at
router 1 competes with the write-back's body flits for the output port towards 3 (different class,
different VC, same link). Under FCFS the write-back's flits, injected earlier, win; the GetS waits
up to 4 ticks, recorded as `NoC Wait = 4`, `NoC Ahead = 4`, with core 1 as the winner.

## 22. Changes from draft 1

| # | review finding | resolution |
|---|---|---|
| 1 | ejection specified two ways | RX slot reserved at VA on the local port; `pushMessage2RX` failure is fatal (§7.3, §8.4) |
| 2 | unloaded-latency formula counted one router per link | `R·router_latency + (R−1)·link_latency + (f−1)`, local links free; example recomputed (§13.2, §21) |
| 3 | injection cost a hidden tick | injection is phase 1, visible in the same tick (§13.2) |
| 4 | TDM slot vs link occupancy | recorded for the deferred real-time phase: a slot is at least `link_latency` ticks (§12) |
| 5 | TDM and priority keyed by core starve the LLC | recorded for the deferred phase: ownership must include the homes; `Candidate.owner` kept now (§12) |
| 6 | `WestFirst` was deterministic yet declared NONE | selects among allowed outputs by credits; genuinely adaptive (§10.2) |
| 7 | routing API could not express the dateline | `Route { out_port, vc_mask }` (§10.1, §10.2) |
| 8 | two classes did not reproduce the legacy split | `num_classes = 2` uses `data == NULL ? 0 : 1` (§6.3) |
| 9 | shared local ports underspecified | `local_ports = 0` default (one per endpoint); sharing defined with per-(endpoint, class) credits and demux (§4.1, §5.2, §8.4) |
| 10 | NI credit view missing | NI specified as an upstream port with credit counters and VC occupancy (§7.1) |
| 11 | an input VC can hold two packets' flits | "current packet" defined as the one at the FIFO front; RC waits for the front (§8.1) |
| 12 | classifier needed home ids | `home_ids` key, derived from the system's `m_shared_memory_id` values (§4.1, §6.3) |
| 13 | trackers need accumulation | `Logger::accumulate` (§15.3) |
| 14 | ENTER through `event()` would change the report path | ENTER trace-only; EXIT through `event()` and `trace()` as today (§7.5) |
| 15 | half-duplex and RX order threaten the equivalence | `half_duplex` key; `rx_serving = alternate` reproducing `MeshInterface`'s alternation; both in the degenerate mapping (§4.2, §7.4, §17.1) |
| 16 | no endpoint or statistics for the network-only harness | `TrafficGenerator` endpoint and `NetworkOnly.csv` (§18); `NetworkStats.csv` (§15.4) |
| 17 | zero latencies undefined | `link_latency ≥ 1`, `router_latency ≥ 0` with stated meaning (§4.2, §13.2) |
| — | review improvements | `class_link_latency` as a documented key; conservation checks first in the test list; the FSM action-to-class mapping scheduled in Phase 1 (§4.2, §20, §14.3) |
| — | user decision 2026-10-09 | real-time arbitration deferred until the infrastructure is built; §12 reduced to the hooks |

## 23. Open issues

1. Whether `electIndex` should replace `elect` for buses too, giving one arbitration code path.
2. Whether `header_bytes` should differ per class (coherence requests are smaller than 8 bytes of
   payload in real fabrics; data headers larger).
3. Local-port bandwidth: one flit per tick per local port is assumed; a wider ejection path would
   need a parameter.
4. The protocol-level deadlock check needs the FSM CSVs to name, per action, the message type and
   class it emits; today that mapping lives in the C++ handlers. A small annotation column in the
   FSM CSVs, or a table in the handler, is required; it is scheduled in Phase 1 (§14.3) and the
   form of the annotation is still to be chosen.
5. The exact byte-identity of §17.1 may be unreachable even with `half_duplex` and `alternate`;
   the fallback is specified but the decision to accept it is the user's.
