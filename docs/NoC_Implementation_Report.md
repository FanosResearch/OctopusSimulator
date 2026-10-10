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

## Step 5 — system wiring

**Built**

- `src/SystemConfigurations/MultiCoreSystem_Mesh.cpp`: `interconnect_type(s),Network` constructs
  a `net::Network` next to the legacy `Mesh` and `NoC` (the three are held behind one
  `interfaceFor(id)` callable, so the controller wiring did not change). Two values come from the
  system level as spec §3.3 asks: `home_ids` defaults to the set of the cores'
  `m_shared_memory_id` when the interconnect map carries none, and the system-level
  `requires_ordering(s)` key is passed to `Network::requireOrdering`, which refuses a routing that
  cannot provide it. An unknown interconnect type now exits 2 with a message instead of using an
  uninitialised pointer.
- `configuration/SystemConfigurations/MultiCoreSystem_Grid.csv`: the 2×2 preset of spec §21.
  L1 0..3 on routers 0..3, the LLC (id 10) on router 3 with its own local port, `XY`,
  `requires_ordering pair_per_class`, injection and ejection queues of 32 (the legacy
  `buffers_max_size`), and `num_classes 2` (below). It is selected with
  `-s MultiCoreSystem_Mesh -c MultiCoreSystem_Grid`; `-p interconnect.node_map[10](i)=0` moves the
  LLC to router 0.
- `header/HangDump.h`: a registry of state dumpers. The network registers its router view at
  construction; a controller's `OCTOPUS_HANG_DUMP` watchdog runs the registry after its own
  dump, the network's own watchdog (no flit moved for `OCTOPUS_HANG_DUMP` ticks while packets
  are in flight, which is what a NetworkOnly deadlock looks like) finalises the raw trace and
  calls `fatal`, and `fatal` runs the registry when the variable is set (spec §16). The
  `OCTOPUS_NET_DEBUG=nocheck` flag skips the construction-time deadlock check so that the
  watchdog can be exercised on a cyclic configuration.
- `src/Protocols/MSIDirectory.cpp`, `LLCMSIDirectory.cpp`: the "Fault Transaction" message now
  prints the controller, line state, address, message id, source, owner, sender, type, kind and
  data flag, as the snoop LLC protocol already did. Three of the step's findings were read off it.
- `test/network/steps/step5.sh`: S5-EEMBC-2x2 (ten traces in parallel, each timed, completion
  judged exactly as `sweeps/sweep_common.sh` judges it), S5-TILING (`OCTOPUS_EVENT_DEBUG=1` so
  the Logger prints its `[EVENT-PATH]` self-check), S5-SPEED, S5-DETERMINISM, S5-PLACEMENT (a
  one-request workload built from the first line of a2time01's core-0 trace, run with the LLC on
  router 3 and on router 0), S5-XPLAT and S5-ASAN (the same a2time01 run under the WSL sanitizer
  build, checksums compared with the Windows run), S5-HANGDUMP and S5-HANGDUMP-POS (a 4-ring
  under `Table` routing, every node streaming 5-flit packets two hops clockwise with
  `vc_depth 2`: refused at construction, or with the check disabled wedged and dumped), and
  S5-RX-STRESS (XFAIL as planned).

**Findings** (each one stopped an EEMBC run; all are recorded in the specification)

- *The injecting endpoint is not `msg.from`.* The network identified a started packet's NI by
  `msg.from` and derived the source router from it. The LLC's responses do not carry the LLC's id
  there (the protocol copies the requester's), so the LLC's body flits were never injected and the
  first run wedged with one head flit delivered. The local port now records the injecting
  endpoint (`holder_ep`) and `newPacket` takes the source endpoint explicitly.
- *Every Octopus interface stamps `msg.from`.* `MeshInterface`, `NoCInterface`, `BusInterface`
  and `TripleBusInterface` all set `msg.from = m_interface_id` in `pushMessage`, and the directory
  L1 tells the home's data (`Data_zeroAck`/`Data_nonZeroAck`, with an ack count) from an owner's
  (`DataFromOwner`) by `from == m_shared_memory_id`. Without the stamp the home's data was taken
  for owner data and `SM_ad` faulted. The NI now stamps it; spec §3.1 and §3.2 say so.
- *A home's PutAck travels with its forwards.* With three classes the LLC's Inv (FORWARD) and its
  later PutAck (RESPONSE) to the same cache are unordered, and the priority RX serving consumed the
  PutAck first: the cache went to I and the Inv then faulted (`(I, Inv)` has no row; the FSM
  handles Inv-then-PutAck through `SI_a`/`MI_a`/`II_a` only). A PutAck overtaking a FwdGetM would
  even lose the forwarded request. The classifier now puts a home's data-less K_ACK in the
  FORWARD class, which is deadlock-safe because an ack is consumed without injecting anything
  (spec §6.3).
- *The shipped directory FSMs assume FIFO over all non-data messages of a pair.* The LLC has no
  row for a PutS arriving in state N: it expects a cache's PutS to reach it before that cache's
  InvAck to the LLC's own eviction Inv, which three classes split into REQUEST and RESPONSE.
  That is exactly the order the legacy request link gave (one FIFO link for every non-data
  message), and the plan already placed three-class protocol operation in Phase 2
  (P2-THREE-CLASS, with S5-RX-STRESS as its XFAIL placeholder). The grid preset therefore ships
  with `num_classes 2`, the legacy split, under which all ten traces complete with both RX
  serving policies; the network's own default stays 3 (NetworkOnly, synthetic traffic). The FSM
  rows that make the protocol tolerant (`(I, Inv)` → InvAck, `(N, PutS/PutM)` → PutAck, and the
  symmetric races) are the content of P2-THREE-CLASS, now written down in spec §6.3.
- *`to` is delivered intact.* The legacy interfaces narrowed `to` to the one destination per
  copy; the directory controllers only test membership, so the spec's bit-identical delivery
  holds and is now stated with that caveat.
- *An empty trace never ends.* A core whose trace file is empty never finishes, so the first
  one-request workload for S5-PLACEMENT (one line for core 0, empty files for the others) ran
  forever; the other cores now get one reference each to other lines, thousands of cycles later.
- *Two legacy bugs under the sanitizer* (S5-ASAN runs a full EEMBC trace, which S4-ASAN's
  synthetic runs never exercised): `Point2PointController` never initialised
  `message_available_upper`/`clk_in_slot_upper` (UBSan: invalid bool load on every run), and
  `CacheController::modifyData` released a `new Message` with `delete[]`, skipping the message
  destructor. Both fixed; the regression suite is identical after the fix.
- *`cond_addr` of the debug print is decimal.* `DebugPrint` parses `cond_addr` with
  `std::stoull(addr)` in base 10, so a hex filter silently matches nothing; worth knowing when
  reading these runs (the unfiltered print was used instead).

**Measurements**

| what | measured |
|---|---|
| EEMBC on the 2×2 grid, directory MSI, two classes | 10 of 10 complete, event-path tiling 275 304 ok / 0 fail / 0 noresp, byte-identical on a second run and on WSL |
| placement (spec §21) | request bus 8, response bus 12 with the LLC on router 3; 2 and 6 on router 0 |
| speed, a2time01 sequential | grid 5.4 s, legacy NoC 4.1 s (1.3×); the ten-trace parallel batch 6–11 s per trace |
| hang dump | a 4-ring under `Table` with the check disabled wedges within 2000 ticks; the dump shows 4 held VCs with zero credits and the run exits 2; with the check on it is refused at construction |
| RX stress (XFAIL) | `eject_queue_depth 2`, two classes: a2time01 completes |

**Tests** (`bash test/network/run.sh 5`, about 6 minutes, the WSL sanitizer run of a2time01 being
most of it):

| id | status | measured | expected |
|---|---|---|---|
| S5-EEMBC-2x2 | PASS | 10/10 complete | complete |
| S5-TILING | PASS | [EVENT-PATH] tiling ok=275304 fail=0 noresp=0 | fail=0 noresp=0 |
| S5-SPEED | PASS | grid 5.4s vs legacy 4.1s (parallel batch: a2time01-trace=8.3s aifirf01-trace=8.7s basefp01-trace=8.7s cacheb01-trace=10. | <= 3x legacy |
| S5-DETERMINISM | PASS | 2 traces byte-identical | identical |
| S5-PLACEMENT | PASS | router 3: 8/12 |  router 0: 2/6 |
| S5-XPLAT | PASS | identical checksums | identical |
| S5-ASAN | PASS | no sanitizer report | none |
| S5-HANGDUMP | PASS | exit 2 |  4 held VCs with zero credits |
| S5-HANGDUMP-POS | PASS | refused at construction | refused |
| S5-RX-STRESS | XFAIL |  | two classes |

Regression suite after the legacy fixes (`Configurable`, `Point2PointController`, `CacheController::modifyData`): REG-BUS, REG-ENV, REG-MESH, REG-TDM all identical.

## Step 6 — degenerate equivalence with the legacy NoC

**Built**

- `configuration/SystemConfigurations/MultiCoreSystem_NetNoC.csv`: the legacy `NoC.csv` matrix
  as a `custom` topology (cores and LLC with direct links, switch 200 as the only explicit
  router), `Table`, one flit per message, `router_latency 1` for the switch, `vc_depth 32` (the
  legacy RX depth), two classes, half duplex, `alternate` serving, `link_model serial`,
  `class_link_latency 2,5`, a large injection queue. The spec's draft-2 recipe (a `star`) could not
  carry the legacy link latencies, because local links cost nothing; §17.1 is rewritten.
- Three engine refinements, all written into the spec as general semantics:
  `link_model serial` delivers a flit `link_latency − 1` ticks after it was sent while holding the
  link for `link_latency` (the legacy `Link::utilize` counts the sending cycle; §4.2); the implicit
  routers of a `custom` matrix add no `router_latency` (they are the endpoint's own link ports,
  §5.1); under `half_duplex` the two directions of a connection form one switch-allocation unit,
  one election over both routers' candidates by age (§11.3), which is what the legacy per-link
  arbiter did. `phaseSwitch` now iterates arbitration units built at construction.
- `rx_serving alternate` moves its selector on every `peekMessage`, as `MeshInterface` does: a
  peek that the controller does not pop (full processing queue) serves the other class next time.
- The packet now accrues its own unloaded transfer time (`pipe`, `transit`), so the wait
  statistic is right on topologies with mixed router latencies.
- `src/Protocols/*Directory.cpp` fault messages print the transaction (from step 5); the
  `[EVENT-PATH]` self-check is used again here.
- `test/network/steps/step6.sh`: S6-IDENTITY (checksums against the REG-MESH references) and,
  when it fails, S6-FALLBACK, which writes every differing Summary column per trace and core to
  `test/network/out/s6/fallback.csv` and judges worst cases and finish cycles.

**What matches exactly**

Every single-message path reproduces the legacy figures to the tick (`NetworkOnly` on the same
topology, and the one-request workload of step 5 through both systems):

| path | legacy | network |
|---|---|---|
| core → LLC, request (direct link, latency 2) | 1 | 1 |
| core → LLC, data (direct link, latency 5) | 4 | 4 |
| core → core, request through the switch | 3 (1 + 1 + 1) | 3 |
| core → core, data through the switch | 9 (4 + 1 + 4) | 9 |
| the one-request workload, all thirteen report columns | 1,1,0,4,7,250,2,266 | identical |

**What does not: S6-IDENTITY fails, S6-FALLBACK fails, the decision is the user's**

All ten traces complete, and the aggregate figures are very close (average latency within
±0.2 cycles on every core, finish cycles within −0.5 % to +0.1 %), but no report is
byte-identical and the per-core worst cases differ. Over the ten traces, 416 Summary cells differ;
the systematic ones:

| column | cells | delta (network − legacy) |
|---|---|---|
| Worst-case Request Bus Latency | 39 | +1 to +19, mean +10.7 |
| Worst-case L1 Access Latency | 36 | +4 to +24, mean +13.8 |
| Worst-case Response Bus Latency | 33 | −7 to +11, mean +0.7 |
| Finish Cycle | 36 | −5769 to +3722, mean −1450 (of ~1 000 000) |
| Average Latency | 36 | −0.1 to +0.2 |

The request-bus histogram of a2time01 core 0 shows the shape: the legacy NoC has 51 887 / 14 542 /
1 952 / 333 / 86 / 22 / 4 requests at 0 / 1 / 2 / 3 / 4 / 5 / 6 ticks and one at 16; the network
has 52 260 / 11 222 / 3 177 / 895 / 444 / 327 / 260 at the same values and a tail to 16. The cause
is the network interface, not the links or the switch:

- *One flit per tick per endpoint.* The legacy node pushed into one queue per link and every link
  elected in the same cycle, so the LLC's three invalidations and a data response left together.
  The NI injects one flit per tick on its local port, and the implicit router lets one flit per
  tick leave that input port, so the same four messages leave over four ticks.
- *One input VC per class.* A message whose link is busy (the half-duplex request link to a core
  that is sending its own request) blocks the messages behind it in the class VC, where the legacy
  node had a separate queue per link. This head-of-line blocking is the request-bus tail.
- The L1-access worst cases move with the request-bus tail (the column spans the request's wait
  to enter the network).

Both are properties of a real network interface that the engine models on purpose. Reproducing
the legacy numbers would need an emulation mode: unlimited injection per tick, one virtual output
queue per link at the endpoint, and arrival-order forwarding at the switch. That is not a network
feature, it is the old model's queue structure, and it would exist only to pass this test. The
recommendation is to accept the divergence as documented here (the manual's NoC chapter carries
the table above), keep the legacy `NoC` class selectable, and not introduce the `NoC` alias
(S6-NOC-ALIAS stays XFAIL). The user decides; S6-FALLBACK records the deltas file either way.

**Decision (2026-10-10): accepted by the user.** `test/network/ref/s6-accepted.txt` records it and
turns S6-IDENTITY and S6-FALLBACK into XFAILs; the legacy `NoC` class stays selectable and no alias is
introduced.

**Tests** (`bash test/network/run.sh 6`, about 2 minutes; step 4 re-run after the
switch-allocation restructure: 0 failures)

| id | status | measured | expected |
|---|---|---|---|

Regression suite: REG-BUS, REG-ENV, REG-MESH, REG-TDM identical.

## Step 7 — observability

**Built**

- `Logger::Role::NOC` (index 12) with `comp = router id`: ENTER when a head flit is appended to an
  input VC (at injection and on arrival over a link, flags `in_port << 8 | in_vc`), SERVICE when
  the head wins VC allocation (`out_port << 8 | out_vc`), EXIT when the tail flit is sent on the
  output, link or local (`out_port << 8 | flits`). The network's REQ_BUS/RESP_BUS trace records
  (one interval per packet from injection to ejection) now carry the destination endpoint in
  `flags`, which is what tells the copies of a multicast apart; the legacy buses and mesh leave
  the field 0 (`docs/Trace.md`).
- Four trackers in the LatencyReport, present only when a `Network` is the interconnect so the
  legacy reports are byte-identical: `NoC Hops`, `NoC Wait`, `NoC Transfer`, `NoC Ahead`, plus
  `NoC Ahead Winner` (the endpoint that beat the request's flits most often, −1 if never beaten).
  They are accumulated over the request's packets through the new `Logger::accumulate` and
  `accumulateWinner`, written at each packet's ejection; the footer and `Summary.csv` gain
  `Worst-case NoC Wait` and `Worst-case NoC Ahead`. The packet keeps per-owner win counts
  (`ahead_by`) for the winner; `NetworkStats.csv`'s `ahead` row gained the winner too.
- The report's response-bus stamp is taken from the copy that reaches the requester: a supply
  goes to the requester and to the home, and on a mesh the home's copy can eject first.
- `tools/octoviz`: `tracefile.py` knows the NOC resource; `occupancy.py` builds one lane per router
  from ENTER→EXIT pairs keyed on (router, msg_id, kind), keys the bus pairing on the new flags,
  and its response-bus check uses the flag to pick the requester's copy; `convert.py` reads the
  five extra report columns when the header announces them. The viewer discovers lanes from the
  data, so router lanes appear without a UI change (S7-VIEWER is the manual check).
- `test/network/s7check.py` (trace-versus-report checks: hop counts, tracker sum, time base),
  `test/network/s7lanes.py`, `test/network/steps/step7.sh`.

**Findings**

- *Requests complete at `Ready + CPU + Total` in trace cycles*, and the Logger accumulates a
  packet's trackers only until the request's row is written: a supply's copy to the home that
  ejects after the requester's copy is not part of the request's `NoC Hops`/`Wait`/`Transfer`.
  The checks apply the same cut-off; nine requests of a2time01 whose two supply copies straddle
  the completion cannot be split from the trace alone and are skipped by the hop check.
- *Copies of one message overtake each other.* The first version of the checks and the
  converter paired a message's bus ENTER and EXIT records in FIFO order; a supply injected first
  towards the home but ejected last (its path is longer) was paired with the requester copy's
  ejection, which showed up as 7 to 12 response-bus mismatches in the converter's self-check and
  13 946 hop mismatches. Stamping the destination in `flags` makes every pairing exact.
- *The time base holds*: every bus ENTER has a router ENTER in the same trace cycle and every
  bus EXIT a router EXIT, for all 461 075 bus records of a2time01; the converter's request-bus
  and response-bus ends agree with the Logger rows without any calibration beyond the bus lanes'
  existing one.
- *The contention example of spec §21 reproduces* with a 9-flit write-back (the spec's 5-flit
  one leaves at most one body flit for a request that arrives a router away): core 0's request
  loses four switch elections to owner 1 and reports `NoC Wait = 4`, `NoC Ahead = 4`, winner 1;
  alone it loses none.

**Tests** (`bash test/network/run.sh 7`, about 4 minutes; the regression suite is identical, the
NoC columns being present only with a `Network`)

| id | status | measured | expected |
|---|---|---|---|
| S7-TRACE-HOPS | PASS | 711320 0 checked/mismatches | 0 mismatches |
| S7-TRACKER-SUM | PASS | 275304 0 checked/mismatches | 0 mismatches |
| S7-TIMEBASE | PASS | 461075 0 checked/mismatches | 0 mismatches |
| S7-CONVERT | PASS | all checks mismatches=0 |  4 router lanes |
| S7-AHEAD | PASS | ahead 4 to owner 1 |  wait 4; alone 0 |
| S7-VIEWER | XFAIL |  | manual: the selected request's route across the router lanes is checked by eye once per release |

Converter self-check on the traced a2time01 run: reqbus_end 63 034 / 0 mismatches, respbus_end 63 034 / 0, array_start 15 553 / 0, dram_length, fill_end and memread_leg 3 385 / 0 each; four NOC lanes.

## Step 8 — the BookSim comparison

**Built**

- `test/network/booksim/mesh44_dor.cfg`: BookSim 2 configuration mirroring S4-SAT-DEF (4×4
  mesh, dimension-order routing, one VC, 4-flit buffers, 5-flit packets, uniform random traffic,
  `sim_type latency`), with BookSim's input-queued router (VA, SA and ST one cycle each,
  `st_final_delay` must be 1 or BookSim asserts), one-cycle links and credits.
- `test/network/steps/step8.sh`: builds BookSim 2 under WSL into `test/network/out/booksim2`
  (a shallow clone of `github.com/booksim/booksim2`; this machine's WSL has no bison, flex, m4 or
  sudo, so the three were built from source into `$HOME/local` with `CFLAGS=-std=gnu17`, GCC 15
  defaulting to C23 which old gnulib does not compile under), sweeps both simulators and
  compares their **saturation throughputs**: the accepted flit rate per cycle per node at the first
  offered rate past saturation (BookSim: its own "simulation unstable" verdict; Octopus: accepted
  below 0.9 of offered, as in S4-SAT-DEF).

**Finding: buffer residency is the difference, and it maps onto the credit loop**

With the same 4-flit buffers and the shipped pipeline (`router_latency 2`, `credit_latency 1`)
Octopus carries 0.48 flits/cycle/node and BookSim 0.28, a 45 percent gap. The cause is where a
flit's buffer slot is released. A BookSim slot stays occupied until the flit has gone through
switch allocation and switch traversal (two cycles after it is eligible), and the credit then
takes `credit_delay`; an Octopus VC slot is released the tick the flit is forwarded (a body flit
is forwarded in its arrival tick), and the credit takes `credit_latency`. So BookSim's credit loop
is about four cycles longer, and under load the 4-flit buffers behind a blocked packet drain
that much more slowly. The sensitivity measurements that established this:

| simulator | configuration | saturation throughput (flits/cycle/node) |
|---|---|---|
| BookSim | 4-flit buffers, 1 VC (the reference) | 0.28 |
| BookSim | 8-flit buffers | 0.45 |
| BookSim | 16-flit buffers | 0.50 |
| BookSim | 2 VCs, 4-flit buffers | 0.55 |
| BookSim | speculative VA/SA | 0.30 |
| Octopus | `vc_depth 4`, rl 2, cl 1 (shipped) | 0.48 |
| Octopus | `vc_depth 2`, rl 2, cl 1 | 0.30 |
| Octopus | `vc_depth 4`, rl 3, cl 2 | 0.37 |
| Octopus | `vc_depth 4`, rl 2, cl 3 | 0.37 |
| Octopus | `vc_depth 4`, rl 3, cl 3 | 0.33 |
| Octopus | `vc_depth 4`, rl 2, cl 4 | 0.32 |
| Octopus | `vc_depth 4`, rl 3, cl 4 (BookSim's pipeline mapped) | 0.29 |

The mapping is `router_latency = VA + SA + ST = 3` and `credit_latency = SA + ST + credit_delay
= 4`; with it the two simulators agree to 3.5 percent (0.289 against 0.279), and the test runs
Octopus with those two keys. The shipped defaults are kept: a slot released on forwarding with a
one-tick credit is the faster router Octopus means to model, and the spec's `credit_latency` row
now says that a longer residency is modelled there (§4.2, §20). The BookSim side of the sweep is
in `test/network/out/s8/booksim.txt` and the Octopus side in `octopus.txt`.

**Tests** (`bash test/network/run.sh 8`, about 2 minutes once BookSim is built; the first run
builds it, a minute more)

| id | status | measured | expected |
|---|---|---|---|
| S8-BOOKSIM | PASS | saturation throughput Octopus 0.288 (offered 0.35) vs BookSim 0.279313 (offered 0.30) flits/cycle/node |  rel. diff 0.03 |

## Phase 2, P2-THREE-CLASS — the directory protocol on three virtual networks

Step 5 had left the protocol presets on two classes because the three-class runs faulted. The
user asked to see the full coherence story of a faulting line before any fix; the tool for that
is `test/network/linetable.py` (every FSM transition of every controller on the line, in time
order, interleaved with the injection and ejection of the messages that carried it, read from
the raw trace). Two distinct mechanisms came out, and each got its own fix.

**Mechanism 1: the NI's injection priority reordered a source's own messages** (a2time01, line
0x7f79d30f7b00). The LLC evicts the line and invalidates its four sharers; L1 0 evicts the same
line at the same time: at 142131 it goes `S --Replacement--> SI_a` and pushes its PutS, at
142132 the Inv reaches it, `SI_a --Inv--> II_a`, and it pushes its InvAck. Its local port is
busy with a 5-flit data supply for another line until 142135, so both wait in the NI; the NI
then served by class priority (spec §7.2 as drafted: RESPONSE before REQUEST): the InvAck went
at 142136, the PutS at 142137. The LLC counted the InvAck as the last one, finished the eviction
(`N_a --last_InvAck--> N`), and received the PutS in `N`, a Fault cell. Fix: the local port goes
to the oldest candidate across classes (injection order, through the port's arbiter) instead of
class priority; there is no deadlock reason to prefer responses at injection, the virtual
networks keep separate buffers downstream. With that alone all ten traces complete on three
classes, also with `eject_queue_depth 2`.

**Mechanism 2: the fabric reorders two classes under contention** (cacheb01 under `RRArbiter`
with `eject_queue_depth 2`, line 0x7fbb082ca5c0). L1 1 injected its PutS at 241475 and its
InvAck at 241479, in causal order; the InvAck reached the LLC at 241484, the PutS at 241492 (17
ticks in flight against 5): the REQUEST-class packet lost round-robin elections to other
traffic while the RESPONSE-class one flowed. Same fault, `(N, PutS)`. This one is inherent to
virtual networks and is what the protocol must tolerate. Fix: the LLC FSM rows
`(N, PutS)`, `(N, last_PutS)`, `(NS_d, …)`, `(NM_d, …)` now answer PutAck and stay, exactly what
the FSM already did in `N_a`; the cache is in `II_a` waiting for that acknowledgement. The L1
side needs no row: a home's Inv or Fwd and its later PutAck to the same cache both travel in
the FORWARD class (step 5's choice), and `pair_per_class` routing keeps them in order, so
`(I, Inv)` and `(I, Fwd*)` stay unreachable.

**Also found**: `inject_queue_depth 2` ends every run with "Cannot insert the Msg into the lower
interface FIFO, FIFO is Full" from the LLC. The controllers do not retry a refused push to their
lower interface (the legacy mesh interface never refused, its TX bound was ineffective), so the
NI's injection queue is the controllers' transmit buffer and must be sized for their bursts; the
presets keep 32. Making the controllers back-pressure-aware on the lower interface is a separate
item.

**Changes**: `Network::phaseInject` (age order), spec §7.2 and §6.3, `Protocols_FSM/MSI_LLC_directory.csv`
(six cells, no new states), `MultiCoreSystem_Grid.csv` ships `num_classes 3`, S5-RX-STRESS is
a must-pass over all ten traces under both arbiters, the plan's P2-THREE-CLASS row is closed.

**Tests** (`bash test/network/run.sh all` on the three-class preset)

Full suite on the three-class preset (`bash test/network/run.sh all`): 59 PASS, 4 XFAIL (S4-ORDER-ADAPTIVE, S6-IDENTITY, S6-FALLBACK, S6-NOC-ALIAS, S7-VIEWER are the expected ones; S6-* by the user's acceptance), 0 FAIL once the WSL sanitizer binary was rebuilt with the age-order injection (a stale binary had made S5-XPLAT differ). S5-RX-STRESS: three classes, eject_queue_depth 2, FCFS and RR: 20/20 complete.
