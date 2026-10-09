# Octopus Network-on-Chip — implementation report

Running record of the implementation of `docs/NoC_Architecture_Spec.md`, step by step as laid out
in `docs/NoC_Test_Plan.md`. One section per step: what was built, the decisions taken where the
specification left room, what the tests measured, and the commit. Written on branch `network`,
off `esweek-tutorial` at `3e4add57`.

## Step 0 — infrastructure

**Built**

- `test/network/unit/`: a 60-line test framework (`testing.h`: `TEST`, `CHECK`, `CHECK_EQ`,
  prefix filter, `PASS`/`FAIL` lines) and the `Octopus_tests` executable, a CMake target linking
  `Octopus_core` so tests drive the real classes. One source file per step; step 1's is empty for
  now so the target builds.
- `test/network/run.sh`: the single runner. Builds, runs the regression suite and the requested
  step's tests, writes `test/network/out/results.csv` (id, status, measured, expected, seconds),
  exits non-zero on any `FAIL`. `XFAIL` lines are reported separately.
- `test/network/asan.sh`: the AddressSanitizer + UBSan build under WSL into `build_asan/`.
- Preset placeholders with the specification's keys and defaults: `configuration/Interconnect/
  Network.csv` (every key of spec §4 with its default, so later steps only change values),
  `MultiCoreSystem_Grid.csv` (a copy of the mesh preset until step 5) and `NetworkOnly.csv`
  (filled at step 4).
- References under `test/network/ref/`: `legacy-mesh/<trace>.md5` for the ten EEMBC traces on
  the legacy single-switch NoC, `reg-tdm.md5` for the TDM bus run, and a committed copy of the
  shipped `MultiCoreSystem.csv`.

**Decisions**

- The regression runs do not read the working copy's `MultiCoreSystem.csv`, which carries local
  edits on this machine; they pass the committed copy with `-c`, which the CLI accepts as a path.
  So the suite is independent of whatever the user is experimenting with in `configuration/`.
- Outputs go through `-o` into `test/network/out/`, never into `BMs/`.
- Checksums strip carriage returns, so references taken on Windows compare equal on Linux.
- `REG-MESH` runs its ten traces in parallel; they are independent processes with separate
  output directories.

**Tests** (`bash test/network/run.sh 0`, Windows, MinGW Release build; the sanitizer build is the
WSL g++ 15 Debug build with `-fsanitize=address,undefined`):

| id | status | measured | seconds |
|---|---|---|---|
| REG-BUS | PASS | identical | 4.5 |
| REG-ENV | PASS | 15364/377 | 5.8 |
| REG-MESH | PASS | identical | 10.3 |
| REG-TDM | PASS | identical | 4.6 |
| S0-HARNESS | PASS | - | 0 |
| S0-RUNNER | PASS | - | 0 |
| S0-REF | PASS | - | 0 |
| S0-ASAN | PASS | - | 0 |
| S0-PRESETS | PASS | - | 0 |

The legacy mesh references cover all ten EEMBC workloads on `MultiCoreSystem_Mesh` (directory MSI
on the single-switch NoC), four cores each, about ten seconds for the ten in parallel.

**Commit**: `ede914fa` "NoC step 0: test harness, runner, references, preset placeholders".

## Step 1 — foundations

**Built**

- `header/Interconnect/Network/NetTypes.h`: the class constants, `Packet` and `Flit`, the flit-count
  formula of spec §6.2, `ConfigError`, and the declaration of `fatal()`.
- `Classifier` (`Classifier.h/.cpp`): the three-class rules keyed on `Message::kind` with the
  fallback rules and the fallback counter; the legacy two-class rule `data == NULL ? 0 : 1`;
  `classifyOrFatal` and a `describe()` that prints a message's fields for the fatal path.
- `Topology` (`Topology.h/.cpp`): generators `grid2d`, `torus2d`, `ring`, `star`, and `custom`
  from a connection matrix; fixed port numbering (east, west, north, south, then local ports;
  clockwise, counter-clockwise for rings; matrix-row order for custom); far-end port resolution;
  endpoint attachment with `local_ports = 0` meaning one port per endpoint and sharing otherwise;
  `describe()` for the hang dump and messages.
- `Fatal.cpp`: the single fatal path, `Network: <message>` on stderr, exit status 2.
- Registered in `src/Interconnect/CMakeLists.txt` and the include path; the unit tests in
  `test/network/unit/step1_foundations.cpp`, run by `steps/step1.sh`.

**Decisions**

- The class constants are `CLASS_REQUEST`, `CLASS_FORWARD`, `CLASS_RESPONSE` rather than the
  spec's bare names, because `MessageType::REQUEST` already exists at global scope in
  `CommunicationInterface.h` and the bare name was ambiguous in every file that includes it.
- **Custom matrices give every endpoint an implicit router of its own.** The spec says nodes
  outside `component_ids` are routers; it is silent on endpoint-to-endpoint edges, which the
  shipped `Mesh.csv` (a complete graph of endpoints, no switch) is made of. Giving each endpoint
  node an implicit router with the endpoint on its local port keeps both shipped matrices loadable
  and makes the legacy direct-link mesh a network of five routers with one endpoint each. The
  legacy equivalence of step 6 uses `star`, where endpoints attach to one router directly, so this
  choice does not affect it. Recorded as a deviation to fold back into the spec.
- Construction errors are `ConfigError` exceptions carrying the message; the `Network` object will
  turn them into `fatal()`. This is what lets the unit tests check refusal messages without a
  child process. The exit-status side of `S1-CLS-FATAL` is therefore covered by the shell tests of
  step 4 rather than by a unit test.
- The classifier's fallback counter is incremented before the fallback rules and decremented again
  when none matches, so it counts only messages that were actually classified by a fallback.
- A torus needs at least 3 routers per dimension, otherwise the wrap link would coincide with the
  direct link; the generator refuses smaller dimensions.
- Port names (`east`, `west(wrap)`, `to 10`, `local0`) are kept per port for the hang dump and the
  error messages of the later steps.

**Tests** (`bash test/network/run.sh 1`; the same 17 tests also pass under the WSL sanitizer build,
`build_asan/test/network/unit/Octopus_tests S1`):

| id | status |
|---|---|
| S1-CLS-3 | None |
| S1-CLS-2 | None |
| S1-CLS-FATAL | None |
| S1-CLS-LOG | None |
| S1-FLITS | None |
| S1-GRID-3x3 | None |
| S1-GRID-COORD | None |
| S1-TORUS | None |
| S1-RING | None |
| S1-STAR | None |
| S1-CUSTOM-MESH | None |
| S1-CUSTOM-NOC | None |
| S1-CUSTOM-ASYM | None |
| S1-ATTACH | None |
| S1-ATTACH-MISSING | None |
| S1-ATTACH-SHARED | None |
| S1-KEYS | None |

Regression suite: REG-BUS, REG-ENV, REG-MESH, REG-TDM all identical.

**Commit**: `fc7b30b6` "NoC step 1: packets, flits, classifier, topology".

## Step 2 — routing and the routing-level deadlock check

**Built**

- `Routing.h/.cpp`: `RoutingAlgorithm` with `nextHop`, `ordering`, `needsEscapeVC`,
  `deadlockFreeOn`, `name` and a `path()` helper that follows `nextHop` without adaptivity;
  `Route {out_port, vc_mask}`; `RouterView` (credits only) as the one piece of router state a
  routing function may read; `RoutingParams` (VCs, escape VC, CSV table); the factory
  `createRouting(name, topology, params)`.
- `Table`: BFS shortest paths from every destination, ties by lowest port index, CSV entries
  overriding single (router, destination) cells with port validation; the escape VC excluded from
  the mask when configured, and a packet already on the escape VC kept there.
- `XY`: dimension order on `grid2d`; on `torus2d` the shorter direction per dimension and the
  dateline rule as a function of (source, destination): a leg that wraps uses VC 1, otherwise VC 0.
- `WestFirst`: west first when the destination is west, otherwise among the minimal outputs the
  one with the most credits of the class, ties by lowest port, so it is genuinely adaptive.
- `AdaptiveMinimal`: most credits among minimal outputs excluding the escape VC; falls back to XY
  on the escape VC when on it or when no adaptive credit exists.
- `channelDependencyAcyclic`: the channel dependency graph over (router, link port, VC) built from
  every source/destination path with the per-hop `vc_mask`, DFS cycle detection, and a readable
  cycle (router, port name, VC) on failure.
- `checkOrderingRequirement`: refuses `pair_per_class` with an algorithm declaring `NONE`, or
  with several VCs per class unless the torus dateline pins the VC.

**Decisions and findings**

- **Paths to a router without endpoints end on a virtual local port** at index `linkPorts(r)`,
  so the deadlock check and the hop-count tests work on every router, not only on those with
  caches attached.
- **Rings are not routable with `Table` and one VC.** The checker, correctly, finds a cycle on
  every ring whose minimal paths are longer than one hop (an even ring through tie-breaking, an
  odd ring through the two-hop paths chaining around it). This is the textbook ring deadlock that
  only a dateline VC removes, and `Table` has no dateline. The test plan's expectation that the
  BFS table on a ring passes was wrong and has been corrected (S2-CDG-ESCAPE, S2-CDG-SHIPPED
  assert the refusal). Consequence for the spec: the `ring` topology needs a ring-aware routing
  with a dateline on VC 1, the same rule `XY` applies on the torus; it is added to the open
  issues and scheduled with Phase 2. Until then a ring configuration is refused at construction,
  which is the honest behaviour.
- The escape-VC check for `Table` is conservative: the dependency chain follows the lowest
  allowed VC of each hop; with single-bit masks (dateline, escape) it is exact.
- `TDM_XY` is refused with a message saying it is deferred, as the specification requires.

**Tests** (`bash test/network/run.sh 2`; the same 15 pass under the sanitizer build):

| id | status |
|---|---|
| S2-TABLE-BFS | None |
| S2-TABLE-TIES | None |
| S2-TABLE-CSV | None |
| S2-TABLE-BADPORT | None |
| S2-XY | None |
| S2-XY-LOCAL | None |
| S2-XY-TORUS-VC | None |
| S2-XY-TORUS-1VC | None |
| S2-WF-TURNS | None |
| S2-AM-MASK | None |
| S2-CDG-CYCLE | None |
| S2-CDG-ESCAPE | None |
| S2-CDG-SHIPPED | None |
| S2-ORDERING | None |
| S2-REQUIRES | None |

Regression suite: all four identical.

## Step 3 — arbiter extension

**Built**

- `Arbiter.h`: `struct Candidate { index, owner, seq }` and the virtual
  `electIndex(cycle, candidates)`; the base returns -1 (not supported), which is what
  `TDMArbiter` inherits until the real-time phase.
- `FCFSArbiter::electIndex`: lowest `seq`, ties by lowest index.
- `RRArbiter::electIndex`: the same rotation as `elect()`, through the same `selectCandidate`
  and `candidate_index`: advance the pointer over the owner ids, the first owner with a candidate
  wins with its lowest-`seq` candidate, the pointer rests on the winner, and an owner without a
  candidate does not consume a turn.

**Decisions**

- The bus-side `elect()` is untouched, so every bus configuration keeps its behaviour; the
  regression suite, which includes a TDM bus run specifically to guard these files, is identical.
- Age on the network side is the injection sequence `seq`; on the bus side it is `Message::cycle`.
  S3-EQUIV feeds both entry points the same draws with `cycle == seq` and checks the winners agree
  for both policies over a thousand draws, which is the evidence that the two paths are one policy.

**Tests** (`bash test/network/run.sh 3`; the four S3 tests also pass under the sanitizer build):

| id | status |
|---|---|
| REG-BUS | PASS |
| REG-ENV | PASS |
| REG-MESH | PASS |
| REG-TDM | PASS |
| S3-FCFS | None |
| S3-RR | None |
| S3-EQUIV | None |
| S3-STATE | None |

## Step 4 — the engine on synthetic traffic

**Built**

- `header/Interconnect/Network/Network.h`, `src/Interconnect/Network/Network.cpp`: the controller.
  `NetConfig` holds every spec §4 key (deferred keys and `TDMArbiter`/`PriorityArbiter` are refused
  at construction, `routing = auto` picks `XY` on grids and tori and `Table` elsewhere).
  `buildTopology` reads `connection_matrix[n]`, `node_map[id]` and `routing_table[id]`, generates the
  topology, creates the routing plug-in and runs the channel-dependency and ordering checks of
  step 2. `buildRouters` allocates the `[port][class][vc]` input FIFOs, the per-output-VC credit
  counters (`vc_depth` for link ports, packet credits for local ports) and one VC-allocation and
  one switch-allocation arbiter per output port. The tick is the six phases of spec §13.2:
  `phaseInject`, `phaseArrivals` (flits and credits whose time has come), `phaseRoute`,
  `phaseVCAlloc` (one `Candidate` list per free output VC, `electIndex` of step 3, RX reservation
  on local ports), `phaseSwitch` (one winner per output port, link busy check, half-duplex check,
  one flit per input port) and ejection on the tail (`eject` stamps the Logger, updates the
  statistics and delivers to the NI). `writeStats` emits `NetworkStats.csv` with the sections
  `port`, `class`, `classtime`, `endpoint`, `received`, `ahead`, `generator`, `totals` and `config`.
  `dump()` is the router view for the hang dump. `OCTOPUS_NET_DEBUG = credit | rx | mask`
  injects the three faults the fatal-path tests need.
- `NetworkInterface.h/.cpp`: the endpoint side. Per-class injection queues with the injection
  sequence, per-class RX deques with `priority` or `alternate` serving, atomic multicast
  acceptance (a message with `n` destinations is accepted only when all `n` injection slots are
  free), `canReserve / reserve / deliver / rxFree` for the local-port credit, and the
  `CommunicationInterface` contract (`pushMessage`, `peekMessage`, `popFrontMessage`,
  `pushMessage2RX`, `canAcceptRX`, `rollback` returns false as the legacy interface does).
- `TrafficGenerator.h/.cpp` and `configuration/TrafficGenerator.csv`: a `ClockedObj` endpoint with
  the patterns `uniform`, `hotspot`, `transpose`, `neighbour`, `none`; `rate` in packets per
  1000 ticks, `payload_bytes`, `cls`, `packets` or `cycles` budget, `seed`, `consume_after`
  (the RX is not drained before that cycle), `multicast`, `untagged` (an unclassifiable message)
  and `start` (first injecting cycle, added for the wormhole scenario). It counts sent, received,
  retries and out-of-order deliveries per source.
- `NetworkOnly` system (`header/SystemConfigurations/NetworkOnly.h`,
  `src/SystemConfigurations/NetworkOnly.cpp`, `configuration/SystemConfigurations/NetworkOnly.csv`):
  one `Network` plus one generator per endpoint, selected with `-s NetworkOnly`; the shipped file
  is a 4×4 grid, `XY`, sixteen uniform generators. `src/CacheSim.cpp` gained the factory branch.
- `configuration/Interconnect/Network.csv`: all keys of spec §4 with the shipped defaults, plus
  `link_model` (below).
- `test/network/netstat.py` (a query helper over `NetworkStats.csv`), `test/network/steps/step4.sh`
  (37 tests, every expectation computed in the script from the specification's formulas) and
  `test/network/unit/step4_engine.cpp` (S4-RX-ORDER, S4-RESERVE on a `Network` built from a
  hand-made parameter map).

**Decisions and corrections to the specification**

- *One tick is one core cycle.* The spec said two ticks per core cycle. `Network` steps on falling
  edges with `m_clk_period = 50`, so a tick is 100 time units, which is the core period: a 4×4 run
  of 21 476 ticks ended at simulation time 2 147 600. Spec §13.1 now says so.
- *Credit throughput bound.* The spec had `vc_depth / (link_latency + credit_latency + 1)`.
  Because arrivals are applied before switch allocation, a body flit is forwarded in the tick it
  arrives and its credit is usable in the tick it returns, so the bound is
  `vc_depth / (link_latency + credit_latency)`; S4-CREDIT-TPUT measures 0.5, 1, 1 for depths 1, 2,
  4 and S4-CREDIT-LAT 0.5 for `credit_latency 3`, exactly the corrected formula. The head of a
  packet additionally waits `router_latency` downstream, so the tests use `router_latency 0`; the
  spec says where the bound is reached (§9).
- *Link occupancy is a model choice, now a key.* The first implementation held a link for its
  whole latency per flit. That is the legacy `MeshController` transfer model (a message occupies
  the link for `latency` cycles) and the degenerate configuration of spec §17 depends on it, but
  with real flits it makes a 5-tick link carry one flit per 5 ticks, which contradicts the credit
  bound and the unloaded formula (S4-CLASS-LAT measured 34 instead of 20). `link_model(s)` was
  added: `pipelined` (default: one flit per tick, delivered `link_latency` later) and `serial`
  (the legacy model); §17.1 adds `link_model serial` to the degenerate mapping.
- *The unloaded formula needs credits.* `R·rl + (R−1)·L + (f−1)` holds only when a packet never
  waits for a credit on the way, which for `link_latency > 1` requires
  `vc_depth ≥ min(f, link_latency + credit_latency)`; S4-CLASS-LAT runs with `vc_depth 8` and the
  spec states the condition (§13.2).
- *Interleaving is the arbiter's business.* Under `FCFSArbiter` the older packet's flits all win
  switch allocation first, so two 5-flit packets sharing a link eject at 12 and 17 (no
  interleaving); under `RRArbiter` they interleave (mean 15.5, last tail still at 17). The test
  plan's S4-INTERLEAVE row was rewritten to say so instead of assuming interleaving.
- *Saturation is measured on accepted load, not latency.* The generators are closed-loop: a
  refused packet is retried, not dropped, so above saturation the offered load self-limits and the
  mean latency plateaus (24.4 ticks from rate 100 upward on the 4×4). S4-SAT-DEF now defines the
  saturation rate as the first offered rate whose accepted rate is below 0.9 of it, stepped by 10
  packets per 1000 ticks, and the band is 0.4–0.7 of the port capacity (the earlier 0.6–1.0 was a
  link-bound guess; a 4×4 mesh with one VC and 4-flit buffers saturates well below that). The
  measured value is 110 (accepted plateau 95–96 per 1000 ticks per endpoint, 0.48 of capacity);
  S8-BOOKSIM will calibrate it.
- *Implicit routers.* `grid2d`, `torus2d` and `ring` give every router a local port set and
  `node_map` places endpoints; a router without endpoints is a pure switch. `Table` paths through
  such routers use a virtual local hop in the dependency graph (step 2).
- *One flit per input port per tick* is enforced by construction in `phaseSwitch` (an input port
  that already sent this tick is skipped) and additionally guarded in `sendFlit` with a fatal, so
  S4-ONE-PER-INPORT is a construction argument plus a runtime check rather than a scenario.
- *Statistics are written once, on destruction or at exit.* `Network::flushStats` runs from the
  destructor (normal end) and from an `atexit` hook (a `fatal()` or a clock-manager stop that
  exits), whichever comes first; a network that never ticked (unit tests) writes nothing.
- *`Configurable` got a virtual destructor.* `CacheSim` deletes the system through `Configurable*`;
  the sanitizer reported the new-delete type mismatch on `NetworkOnly`. This is pre-existing
  undefined behaviour for every system class; the fix changes nothing observable for the legacy
  systems (the regression suite is identical after it) and makes `~NetworkOnly` run, which
  exposed a second ordering bug: the generators were deleted before the network whose statistics
  read them. The destructor now deletes the network first.
- *`AdaptiveMinimal` never adapted, found by the soak.* Its hotspot soak was byte-identical to
  `XY`'s. The NI injects a packet on the lowest free VC of the local port, which is VC 0, and VC 0
  is also the escape VC, so the rule "a packet on the escape VC stays on XY" fired at the first
  router for every packet. The rule now applies only to packets that arrived over a link on the
  escape VC (spec §10 says so); after the fix the adaptive soak differs (mean 197 vs 136 ticks
  under a hotspot, where spreading over two VCs only adds blocking) and the ordering check sees
  980 reordered pairs under transpose traffic. S2's unit tests are unaffected (they route from a
  link port).
- *S4-ORDER-ADAPTIVE is an XFAIL that reports numbers.* Adaptive algorithms declare
  `Ordering::NONE`, so the test cannot assert a count; it records that `WestFirst` reordered 81
  pairs and `AdaptiveMinimal` 980 under transpose traffic, proving the out-of-order counter is live.
- *S4-CUTTHROUGH* is covered by the 5-flit S4-UNLOADED cases: store-and-forward would give
  `R·(rl + f) + (R−1)·L` = 28 on the 2×2 instead of the measured 12.

**Measurements** (all from `test/network/out/s4/*/NetworkStats.csv`)

| what | configuration | measured | formula |
|---|---|---|---|
| unloaded, 1 and 5 flits | 2×2 corner to corner, `XY` and `Table` | 8 / 12 | `3·2 + 2·1 + (f−1)` |
| unloaded, 1 and 5 flits | 4×4 corner to corner | 20 / 24 | `7·2 + 6·1 + (f−1)` |
| unloaded, 1 and 5 flits | 4×4 torus corner to corner (wrap-around) | 8 / 12 | `3·2 + 2·1 + (f−1)` |
| unloaded, 1 and 5 flits | star | 2 / 6 | `1·2 + 0 + (f−1)` |
| latency keys | 4×4, `router_latency 0`; `router_latency 4, link_latency 3` | 6; 46 | `0 + 6·1`; `7·4 + 6·3` |
| per-class link latency | 2×2, `class_link_latency 2,5,5`, `vc_depth 8` | request 10, response 20 | `3·2 + 2·2`; `3·2 + 2·5 + 4` |
| credit throughput | 2×1, `router_latency 0`, `vc_depth` 1 / 2 / 4 | 0.50 / 1.00 / 1.00 flits per tick | `min(1, d/2)` |
| credit latency 3 | `vc_depth 2` | 0.50 | `2/4` |
| wormhole blocking | `vc_depth 2`, sink stalled 40 cycles | the unrelated packet is delayed 22 ticks | `vc_depth 8`: not delayed |
| half duplex | 40 × 5 flits each way on one connection | 404 ticks vs 204 full duplex | 2× |
| saturation, uniform | 4×4 `XY`, 5-flit packets | offered 110, accepted plateau ≈ 95 per 1000 ticks | band 80–140 |
| saturation, hotspot to node 5 | same | 20 | < uniform |
| saturation, transpose | `XY` 80, `WestFirst` 80 (accepted 65.7 vs 66.1) | WestFirst ≥ XY | |
| soak, hotspot near saturation | 200 000 packets, 5 routing configurations | `XY` 940 885, `Table` 940 885, `WestFirst` 941 107, `AdaptiveMinimal` 953 033, torus `XY` 943 702 ticks, all conserved | |

**Tests** (`bash test/network/run.sh 4`, 2 min 20 s on this machine including the soak and the
curves; `S4-ASAN` runs two simulations and the unit tests S1–S4 under the WSL sanitizer build):

| id | status | measured | expected |
|---|---|---|---|
| S4-UNLOADED | PASS | formula | formula |
| S4-INJECT-TICK | PASS | 2/6 | 2/6 |
| S4-LATENCY-KEYS | PASS | formula | formula |
| S4-CLASS-LAT | PASS | 10/20 | 10/20 |
| S4-CREDIT-TPUT | PASS | min(1 | d/(L+cl)) |
| S4-CREDIT-LAT | PASS | 0.501 | 2/(1+3)=0.5 |
| S4-WORMHOLE | PASS | P3 delayed 22 | 15..30 (the sink stalls ~20) |
| S4-VCT | PASS | 5 | 5 (R=2: 2*2+1) |
| S4-CUTTHROUGH | PASS | covered by S4-UNLOADED 5-flit cases | SAF would be R*(rl+f)+(R-1)L |
| S4-TWO-IN-VC | PASS | 7/7 | 7 |
| S4-HALF-DUPLEX | PASS | half 404 full 204 | >=400 / <=300 |
| S4-INJECT-FULL | PASS | retries 187 |  delivered 50 |
| S4-EJECT-FULL | PASS | 5 delivered in order |  max latency 48 |
| S4-VA-CONTEND | PASS | first owner 0 |  8 and 9 |
| S4-INTERLEAVE | PASS | FCFS mean 14.5 |  RR mean 15.5 max 17 |
| S4-ONE-PER-INPORT | PASS | runtime guard in sendFlit | fatal if an input port sends twice in a tick |
| S4-LOCAL-BW | PASS | max 14 | >= 13 (the second tail ejects >= 5 ticks later) |
| S4-CLASS-ISOLATION | PASS | responses 9 while requests blocked | 9 |
| S4-MULTICAST | PASS | 20 packets |  12 retries |
| S4-SHARED-LOCAL | PASS | 40 delivered through one port | 40 |
| S4-ORDER | PASS | 0 reordered under XY and Table | 0 |
| S4-ORDER-ADAPTIVE | XFAIL |  | adaptive algorithms declare NONE: WestFirst reordered 81 |
| S4-SOAK | PASS | 5 configurations |  200000 packets each |
| S4-FATAL-DEST | PASS | exit 2 |  'not an endpoint' |
| S4-FATAL-MASK | PASS | exit 2 |  'empty vc_mask' |
| S4-FATAL-CREDIT | PASS | exit 2 |  'credit counter above vc_depth' |
| S4-FATAL-RX | PASS | exit 2 |  'without a reservation' |
| S4-FATAL-CLASS | PASS | exit 2 |  'unclassifiable' |
| S4-FATAL-STATUS | PASS | no fatal path exited 0 | never 0 |
| S4-SAT-DEF | PASS | saturation at offered rate 110 (unloaded mean 14.217) | 80..140 (0.4..0.7 of the 200 pkt/1000 ticks port capacity) |
| S4-HOTSPOT | PASS | hotspot saturates at 20 | < uniform (110) |
| S4-TRANSPOSE | PASS | XY 80 |  WestFirst 80 |
| S4-STATS | PASS | local 250000 = ejected 250000; link 664135 = flits x links 664135 | equal |
| S4-CONSERVE | PASS | every run conserved flits and packets | injected = ejected + 0 in flight |
| S4-DETERMINISM | PASS | 3 reruns byte-identical | identical |
| S4-ASAN | PASS | no sanitizer report (2 sims + unit tests S1-S4) | none |

Regression suite after the `Configurable` change: REG-BUS, REG-ENV, REG-MESH, REG-TDM all identical.
