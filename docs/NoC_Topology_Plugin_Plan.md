# NoC topologies as plug-ins: design and plan

Status: T0, T1, T2, T3 and T5 implemented on `network` (2026-11-21), each gated on byte-identical results over a 35-configuration matrix and the full test suite. T4 implemented the same day: `mesh3d` and `torus3d`, `XY` on rings with a dateline, the turn models `NorthLast`, `NegativeFirst` and `OddEven`, the oblivious `O1TURN`, `Valiant` and `ROMM`, weighted `Table`, `switch_latency`, per-router `router_latency`, `sa_allocator`, and new traffic patterns and on/off injection, validated by step T (`bash test/network/run.sh T`). T6 (one-way links and split attachment) is not done. The manual (chapter 9) and `docs/NoC_Architecture_Spec.md` (revision 2026-11-21) describe the result.
Branch: `network`. Related: `docs/NoC_Architecture_Spec.md` §4.1, §5 and §10, `docs/NoC_Test_Plan.md`.

## 1. Goal

Adding a topology should mean adding one file. Adding a routing algorithm should mean adding one
class that states what it needs from the topology. Neither should touch the router engine, the NI,
the classifier or the controllers.

Every step of this plan is a refactor first. Each step must leave every existing result
byte-identical before any new topology or algorithm is added.

## 2. Where the network is rigid today

The engine is already topology-agnostic. `Network.cpp` uses only graph queries on `Topology`:
`routers()`, `ports()`, `linkPorts()`, `isLocalPort()`, `link()`, `routerOf()`, `localPort()`,
`endpointsAt()` and `portName()`. It never asks what shape the network is.

The rigidity sits in four places.

1. **Generators are static members that write private fields.** `Topology::grid2d`, `ring`,
   `star` and `custom` (`Topology.cpp:89-209`) fill `m_routers` directly. `grid2d` and `custom`
   each carry their own copy of the far-end port resolution loop. The generator is chosen by an
   `if` chain on the topology string (`Network.cpp:112-138`), which also reads each generator's
   keys (`dims`, `node_map`, `connection_matrix`, `router_ids`).
2. **The shape is a closed enum with 2-D coordinates.** `Topology::Kind {Custom, Grid2D, Torus2D,
   Ring, Star}` and `std::pair<int,int> coord` with `dimX()` and `dimY()` (`Topology.h:24-52`).
   Users outside `Topology`:
   - `XYRouting`, `WestFirstRouting`, `AdaptiveMinimalRouting` check `kind()` and read `coord()`,
     `dimX()`, `dimY()` (`Routing.cpp:128-231`).
   - `checkOrderingRequirement` accepts several VCs only for `Torus2D` with a routing named `"XY"`
     (`Routing.cpp:322`).
   - `NetworkOnly.cpp:22-26` computes the transpose partner from `coord()` and `dimX()`.
   - `implicitRouter()` is `kind() == Custom && isEndpoint(id)` (`Topology.h:62`).
   - Unit tests `step1_foundations.cpp:117-128` and `step2_routing.cpp:9-17`.
3. **Ports are found by neighbour, not by direction.** `portTowards(t, r, neighbour)`
   (`Routing.cpp:15`) searches the link list for the neighbour router. A topology with two links
   to the same neighbour (a 2-ring, a torus of side 2) is ambiguous, which is one reason
   `torus2d` needs both dimensions at least 3.
4. **One attachment router per endpoint, links in pairs.** An endpoint injects and ejects at the
   same router and local port. `custom` requires a symmetric matrix, so every link has a reverse.
   Unidirectional multistage networks (butterfly, omega, baseline) do not fit.

A smaller issue on the way: `custom` port numbering follows from how its loop is written, not from
a stated rule (`Topology.cpp:186-196`). The loop runs over nodes in ascending id (`adj` is a
`std::map`) and adds each link once, at the lower-id end. So at a node, ports to lower-id
neighbours come first in ascending id, then ports to higher-id neighbours in the node's row order.
The spec and the manual say "row order". §3.0 settles this before the refactor.

## 3.0 Decision: port order on `custom` graphs (2026-11-21)

**What other simulators do** (read in their source, 2026-11-21):

| simulator | rule |
|---|---|
| BookSim 2 `anynet` | endpoint ports first by ascending node id, then router ports by ascending neighbour id (`std::map` order in `AnyNet::_BuildNet`). Input order does not matter |
| gem5 Garnet | port index = link creation order, and `Topology.cc` creates links in nested loops over ascending switch ids. Input order does not matter. Routes come from link weights |
| Noxim | no arbitrary graphs. Ports are a fixed direction enum |

**What the shipped matrices see.** For every shipped matrix (`Mesh.csv`, `NoC.csv`,
`MultiCoreSystem_NetNoC.csv`) today's mixed rule gives exactly row order.

**A hidden dependency.** In `NetNoC`, L1 0's row is `0,200,10`, so its port 0 leads to switch 200
and port 1 to the LLC. An L1-to-L1 message has two 2-hop paths, through the switch or through
the LLC. `Table` breaks the tie to the lowest port, so it goes through the switch, which is what
makes `NetNoC` match the legacy `NoC`. Sorted numbering would put the LLC on port 0 and move
L1-to-L1 traffic through the LLC. The legacy equivalence therefore rests on port numbering
through a tie-break. (Derived from the code, not yet run.)

**Decision.**

1. Port numbers name ports. They must not carry routing intent.
2. The documented rule for `custom` is **row order**: a node's link ports follow the order its
   row lists its neighbours, then its local ports. This changes no shipped result and makes the
   spec and the manual true.
3. Routing preferences are stated explicitly. `NetNoC` gets `routing_table` entries that send
   L1-to-L1 traffic through switch 200. Link weights on `custom` graphs (weighted `Table`,
   Garnet-style) come later and replace such entries where convenient.
4. The `Table` tie-break (lowest port) stays. Once preferences are explicit it only decides
   between genuinely equal paths.

With 2 and 3 in place, a later move to sorted numbering, for BookSim-style canonical networks,
would rename ports without changing any route. That move is not planned.

## 3. Design

### 3.1 A construction API on `Topology`

`Topology` stays the one graph object the engine sees. It gains a public construction API, so
builders no longer need to be members.

```cpp
class Topology {
public:
    // construction (used by builders only; the engine never calls these)
    int  addRouter(int id);                                   // returns the router index
    int  addLink(int a, int b, const std::string &name_ab,    // appends one port on a and,
                 const std::string &name_ba);                 // for a two-way link, one on b
    void attach(int endpoint, int router);                    // in call order
    void setImplicit(int r, bool v);                          // no router_latency (spec 5.1)
    void setAttr(int r, const std::string &key, int value);   // shape metadata, see 3.3
    void setLattice(Lattice l);                               // see 3.2
    void finalize(int local_ports);                           // resolves far-end ports once,
                                                              // numbers local ports, validates
    ...
};
```

`finalize()` replaces the duplicated far-end loops and `finishLocalPorts()`. It also runs the
checks every topology needs: every link has a valid far end, every endpoint is attached once, and
port and VC counts stay within the limits of the channel dependency check (64 ports, 32 VCs,
`Routing.cpp:265`).

### 3.2 Lattice: coordinates as a capability

Mesh, torus, ring and their 3-D forms share one description.

```cpp
struct Lattice {
    std::vector<int>  dims;   // k per dimension
    std::vector<bool> wrap;   // wrap link per dimension (torus, ring)
    std::vector<int> coord(int r) const;
    int routerAt(const std::vector<int> &c) const;
    int port(int r, int dim, int dir) const;    // dir = +1 or -1, -1 if no such link
};
const Lattice *Topology::lattice() const;      // null on a non-lattice graph
```

`port(r, dim, dir)` is stored when the builder adds the link, so routing never searches for a
neighbour. That removes the side-2 ambiguity of item 3 in §2.

A ring is a 1-D lattice with a wrap. A 2-D grid is `dims = {kx, ky}`, `wrap = {false, false}`.
A torus sets both wraps. `mesh3d` and `torus3d` are three dimensions.

**Port-order contract.** Port numbers appear in every report (`NetworkStats.csv` port rows, the
hang dump, the trace flags), so the lattice builder must reproduce today's order exactly:

| topology | link port order | names |
|---|---|---|
| grid2d, torus2d | (x,+1), (x,-1), (y,-1), (y,+1), absent ones skipped on a grid | east, west, north, south, with `(wrap)` on wrap links |
| ring | (x,+1), (x,-1) | clockwise, counter-clockwise |
| 3-D (new) | the 2-D order, then (z,-1), (z,+1) | up, down |

The order is a table inside the lattice builder, not derived from the dimension index, because
the 2-D order is not dimension order (north, y-1, comes before south, y+1).

### 3.3 Attributes and per-router flags instead of `Kind`

`Kind` is removed. What code learned from it becomes explicit data set by the builder.

- `implicitRouter(r)` reads a per-router flag. `custom` sets it on endpoint nodes, as today.
- `attr(r, key, default)` holds shape metadata for non-lattice topologies: `"level"` for trees,
  `"group"` for a dragonfly, `"stage"` for a multistage network. A routing algorithm that needs
  an attribute checks for it at construction and refuses with the attribute named.
- `kindName()` stays as a string for messages and `describe()`.

### 3.4 Builders in a registry

```cpp
struct TopologyBuilder {
    virtual ~TopologyBuilder() {}
    virtual std::string name() const = 0;                       // the value of `topology`
    virtual std::vector<KeySpec> keys() const = 0;              // own keys, types, defaults
    virtual Topology build(const KeyView &cfg, const std::vector<int> &endpoint_ids,
                           int local_ports) const = 0;
};
void registerBuiltinTopologies(TopologyRegistry &reg);         // one add() per builder
```

`KeyView` is a read-only view of the `interconnect.` keys. `keys()` lets the network validate a
builder's keys and lets the manual's key table be generated from the code.

`Network.cpp:112-138` becomes one call:

```cpp
m_topo = std::make_unique<Topology>(TopologyRegistry::create(m_cfg.topology, view, m_component_ids, m_cfg.local_ports));
```

An unknown name lists the registered builders in the error. The shipped builders are `grid2d`,
`torus2d`, `ring`, `star` and `custom`, one file each under `src/Interconnect/Network/topologies/`.

**Registration is explicit, not by a static macro.** The network code is built into
`Octopus_core`, a static library (`src/CMakeLists.txt:1`, no type given and no
`BUILD_SHARED_LIBS`). The linker drops an object file from a static library when nothing
references it, so a builder file that only holds a static registrar would silently disappear. The
registry is therefore filled by one function, `registerBuiltinTopologies()`, with one line per
builder. Adding a topology is then three lines outside its own file: the registration line, one
`target_sources` entry in `src/Interconnect/CMakeLists.txt`, and its keys in
`configuration/Interconnect/Network.csv`.

### 3.5 Routing reads capabilities, not kinds

- `XYRouting` becomes dimension-order routing over `lattice()->dims`, with a dateline on every
  dimension where `wrap` is set. It keeps the name `XY` and, on 2-D, the exact current port and
  VC choices. It refuses a topology without a lattice. Rings, 3-D meshes and 3-D tori then route
  with no new class.
- `WestFirst` and the future turn models (north-last, negative-first, odd-even) require a 2-D
  lattice without wraps.
- `AdaptiveMinimal` requires a lattice. Its escape VC must follow the dateline on wrapped
  dimensions. This also fixes the torus deadlock found in the 2026-11-13 manual review
  (`Routing.cpp:224` ignores the dateline, `Routing.cpp:243` reports it safe).
- The ordering rule becomes a property of the algorithm, `bool fixesVCPerPair() const`, true for
  dimension-order routing with datelines. `checkOrderingRequirement` stops testing names.

### 3.6 Optional: one-way links and split attachment

For unidirectional multistage networks:

- `addLink(a, b, name, oneway = true)` adds a port only on `a`. Credits still return to `a`.
  `half_duplex` ignores one-way links.
- `attach(endpoint, inject_router, eject_router)`. The NI injects at one router and ejects at
  another. This is the only change that reaches the engine: `routerOf()` and `localPort()` split
  into inject and eject versions in `Network.cpp` and `NetworkInterface.cpp`.

This is worth doing only if unidirectional butterflies, omega or baseline networks are needed.
Folded butterflies and fat trees already fit two-way links.

## 4. Companion changes on the routing side

These are not about topologies, but the same refactor is the right time for them. They are listed
here so the interfaces are designed once.

1. **Per-packet routing state.** `nextHop` is `const` and sees a `const Packet*`. Valiant, ROMM,
   XY-or-YX and UGAL choose once per packet. Add a `RouteState` field to `Packet`, a hook
   `onInject(Packet&, Rng&)`, and a seeded per-network generator so reruns stay byte-identical.
2. **All permitted outputs for the deadlock check.** `channelDependencyAcyclic` follows one path
   per pair with no credits (`Routing.cpp:271`), so an adaptive or randomised algorithm is checked
   only along that path and must vouch for itself in `deadlockFreeOn`. Add
   `std::vector<Route> candidates(const RouteContext&) const`, defaulting to `{nextHop(c)}`, and
   build the dependency graph over every candidate. For algorithms with an escape VC the check
   then follows Duato's condition: the escape subnetwork must be acyclic.

## 5. Steps

Each step ends with the full suite run (`bash test/network/run.sh all`, with the benchmark
traces fetched) and every reference unchanged. Steps 1 to 3 add no feature.

| step | change | files | pass criterion |
|---|---|---|---|
| T0 | `custom` numbers link ports in row order (§3.0). `NetNoC` gets explicit `routing_table` entries for its L1-to-L1 routes through switch 200 | Topology.cpp, MultiCoreSystem_NetNoC.csv, spec §5.1 | new unit test: for every shipped matrix, every router's port list is identical before and after. Full suite byte-identical. New unit test: a matrix whose rows are not in ascending order gets row-order ports. New unit test: with `NetNoC`'s L1 rows reordered (`10` before `200`), the L1-to-L1 route goes through 200 with the explicit entries and through the LLC without them, which shows the entries carry the intent |
| T1 | Construction API, `finalize()`, registry, the five builders moved to their own files | Topology.h/.cpp, new topologies/*.cpp, Network.cpp | all REG, S1-S8, P2, MEM, D results byte-identical |
| T2 | `Lattice` with n-D coordinates and `port(r, dim, dir)`. XY becomes dimension-order routing. Migrate WestFirst, AdaptiveMinimal (same behaviour), `NetworkOnly` transpose, unit tests | Topology, Routing.cpp, NetworkOnly.cpp, step1/step2 unit tests | byte-identical. New unit test: for every lattice router and direction, `port()` equals today's `portTowards()` |
| T3 | Per-router implicit flag and attributes. Delete `Kind`. `fixesVCPerPair()` | Topology, Routing.cpp | byte-identical |
| T4 | Features that fall out: ring routing with a dateline, `mesh3d` and `torus3d` builders, AdaptiveMinimal dateline fix | Routing.cpp, new builders | new tests with formula-derived expectations: unloaded latency on a ring and a 3-D mesh, channel dependency check passes on rings of 3 to 8, ordering refusal and acceptance |
| T5 | Companion: `RouteState` and `onInject`, `candidates()` and the full dependency check | NetTypes.h, Routing, Network.cpp | byte-identical for the shipped algorithms. The torus AdaptiveMinimal of today must be refused by the new check before the T4 fix |
| T6 | Optional: one-way links and split attachment | Topology, Network.cpp, NetworkInterface.cpp | new tests on a 2-stage butterfly |

T5 can run before T4 if the check should catch the torus bug first, which is the stronger
demonstration that the new check works.

## 6. Risks

- **Port numbering.** Any change in port order changes routing tie-breaks and every report.
  T2's `port()` versus `portTowards()` test and the byte-identity gate catch it.
- **Router index order.** Builders must add routers in today's order (row-major on grids, matrix
  key order on `custom`). Iteration order in the engine is by index, so it defines the result.
- **Hidden routing intent.** Any equal-cost tie broken by port index is a preference nobody
  wrote down. T0 removes the known one (`NetNoC`). The weighted `Table` routing should print, at
  construction, the number of destinations where a tie was broken, so new ones are visible.
- **Link names.** They appear in `NetworkStats.csv`. Builders keep the current names.
- **Registration.** A builder missing from `registerBuiltinTopologies()` is not linked in. A unit
  test lists the registry and compares it with the builder files.

## 7. Documents to update with the code

- `NoC_Architecture_Spec.md` §5.1 (the row-order rule and the `NetNoC` routes, at T0), §4.1 (keys per builder), §5 (Lattice, attributes, the construction
  API, the port-order contract), §10.1 (capability checks, `fixesVCPerPair`, `candidates`).
- `NoC_Test_Plan.md`: the T-step tests.
- Manual chapter 9 (sections on topology, routing and limitations) and chapter 14 ("add a
  topology" next to "add a routing algorithm").
