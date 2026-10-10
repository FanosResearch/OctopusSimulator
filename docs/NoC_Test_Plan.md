# Octopus Network-on-Chip — test plan

Status: draft 1, 2026-10-10, reviewed the same day; the review's twenty findings are folded in.
Companion to `docs/NoC_Architecture_Spec.md` (what is being built) and `docs/NoC_MultiHop_Plan.md`
(the phases). Every normative sentence in the specification that has an observable effect maps to
a test here; the test id is what a commit message and a failing run refer to.

Contents

1. Conventions
2. Regression suite (runs at every step)
3. Step 0 — infrastructure
4. Step 1 — foundations
5. Step 2 — routing and the routing-level deadlock check
6. Step 3 — arbiter extension
7. Step 4 — the engine on synthetic traffic
8. Step 5 — system wiring
9. Step 6 — degenerate equivalence
10. Step 7 — observability
11. Step 8 — the BookSim comparison
12. Phase 2 additions
13. Traceability

---

## 1. Conventions

- **One runner.** `bash test/network/run.sh [step|all]` builds what it needs and runs every test
  of the named step plus the regression suite (§2); `all` runs everything so far. Each test prints
  one line, `PASS <id>` or `FAIL <id>: measured <x> expected <y>`, and the runner exits non-zero
  if any test fails. A machine-readable `test/network/results.csv` (id, status, measured,
  expected, seconds) is written beside the log.
- **Expected values come from the formula, never from a run.** A test computes its expectation
  from the specification's formulas and the configuration it uses, so a wrong engine cannot
  bless itself. Where a reference run is unavoidable (equivalence, cross-platform) the reference
  is a checksum taken once and committed with the test.
- **Determinism is part of every simulation test** from step 4 on: the test runs twice and the
  outputs it checks (`LatencyReport_C*.csv`, `Summary.csv`, `NetworkStats.csv`, the raw trace when
  enabled) must be byte-identical before any value is compared.
- **Kinds.** `unit` = a C++ test in `Octopus_tests` (§3), no clock loop; `sim` = a simulator run
  on a preset with `-p` overrides, judged from its output files and exit code; `shell` = a check
  of exit codes, messages or file layout.
- **Expected-fail.** A test may be marked `XFAIL <reason>`; the runner reports it separately and
  turns it into a failure when the step that is meant to fix it is reached (§12).
- **No user configuration file is edited by a test.** Tests use the committed presets and `-p`
  overrides, and write under `test/network/out/`.

## 2. Regression suite

Runs at every step; all must stay byte-identical to their references.

| id | kind | what | reference |
|---|---|---|---|
| REG-BUS | sim | shipped `MultiCoreSystem` on `a2time01`, four cores | `tutorial/00-setup-and-first-run/expected/Summary.csv` |
| REG-ENV | shell | `scripts/check_environment.sh` reference run | 15364 requests, worst-case DRAM latency 377 cycles |
| REG-MESH | sim | legacy `MultiCoreSystem_Mesh` (the single-switch `NoC`) on all ten EEMBC traces | checksums of `LatencyReport_C*.csv` and `Summary.csv` taken at step 0 and committed under `test/network/ref/legacy-mesh/`; the legacy files are not touched before step 6, so any drift is a build or arbiter regression |
| REG-TDM | sim | shipped bus with `TDMArbiter` on `a2time01` | checksum taken at step 0; guards the arbiter files that step 3 edits |

## 3. Step 0 — infrastructure

Deliverables, each with a check that it exists and runs:

| id | kind | what |
|---|---|---|
| S0-HARNESS | shell | `Octopus_tests` executable, a CMake target linking `Octopus_core`, one source file per step under `test/network/unit/`, built by the normal `cmake --build`; runs and prints `PASS S0-HARNESS` with zero tests |
| S0-RUNNER | shell | `test/network/run.sh` exists, runs §2, writes `results.csv` |
| S0-REF | shell | the REG-MESH and REG-TDM references are generated and committed |
| S0-ASAN | shell | a `build_asan/` configuration (`-fsanitize=address,undefined`, Debug) builds on WSL; used by S4-ASAN and S5-ASAN |
| S0-PRESETS | shell | `configuration/Interconnect/Network.csv`, `NetworkOnly.csv`, `MultiCoreSystem_Grid.csv` exist as placeholders that the later steps fill in |

## 4. Step 1 — foundations

All `unit`.

| id | assertion | expected |
|---|---|---|
| S1-CLS-3 | 20 hand-built messages, one per `kind` and one per fallback rule, classify under `num_classes = 3` | the class column of spec §6.3, exactly |
| S1-CLS-2 | the same messages under `num_classes = 2` | `data == NULL ? 0 : 1` for every message, `kind` ignored |
| S1-CLS-FATAL | a message matching no rule (`kind` unknown, `data == NULL`, neither endpoint a home) | fatal, exit status 2, message prints the fields |
| S1-CLS-LOG | ten fallback-classified messages | the once-per-run fallback count printed is 10 |
| S1-GRID-3x3 | `grid2d` 3×3: neighbour sets and port order of every router | hand-written table: centre router has ports east, west, north, south, local; corners two link ports then local; edges three |
| S1-GRID-COORD | `coord(r)` for 4×4 | `r = y·4 + x` inverted exactly |
| S1-TORUS | `torus2d` 3×3 wrap links | every router has four link ports; the wrap connection is two unidirectional links |
| S1-RING | `ring` 5 | clockwise then counter-clockwise then local |
| S1-STAR | `star` with five endpoints | one router, five local ports, no link ports |
| S1-CUSTOM-MESH | the shipped `Mesh.csv` matrix | adjacency equals what `Mesh::Mesh` builds from the same matrix |
| S1-CUSTOM-NOC | the shipped `NoC.csv` matrix | node 200 is a router, the rest endpoints; adjacency as the legacy code |
| S1-CUSTOM-ASYM | a matrix where 0 lists 1 but 1 does not list 0 | refused; message names the pair (0, 1) |
| S1-ATTACH | 4×4 with `node_map` for five endpoints, `local_ports = 0` | each endpoint has its router and its own local port; `endpointsAt`, `localPort`, `routerOf` agree |
| S1-ATTACH-MISSING | an endpoint without `node_map` | refused; message names the endpoint |
| S1-ATTACH-SHARED | two endpoints on one router with `local_ports = 1` | both on port index `links`; `endpointsAtPort` lists both |
| S1-FLITS | flit count for payload 0, 64, 72 bytes at `flit_bytes` 16, 8, 0 and `header_bytes` 8 | 1, 5, 5 at 16; 1, 9, 10 at 8; 1, 1, 1 at 0 |
| S1-KEYS | every constraint row of spec §4.2 violated one at a time | each refused, message names the key |

## 5. Step 2 — routing and the routing-level deadlock check

All `unit`.

| id | assertion | expected |
|---|---|---|
| S2-TABLE-BFS | `Table` on 4×4 by BFS: hop count for all 240 pairs | Manhattan distance |
| S2-TABLE-TIES | two equal-length options | lowest port index chosen, checked on three hand-picked pairs |
| S2-TABLE-CSV | `routing_table[5]` naming two destinations | those two entries from the CSV, the rest from BFS, verified entry by entry |
| S2-TABLE-BADPORT | a table naming a port the router lacks | refused; message names router and port |
| S2-XY | all 240 pairs on 4×4 | the X-then-Y path from an independent two-line reference implementation |
| S2-XY-LOCAL | destination router reached | the local port of the destination endpoint |
| S2-XY-TORUS-VC | 4×4 torus, a packet whose route crosses the wrap in X | `vc_mask = 0b10` at every hop in that dimension, `0b01` otherwise |
| S2-XY-TORUS-1VC | `torus2d` + `XY` + `vcs_per_class = 1` | refused at construction |
| S2-WF-TURNS | `WestFirst` on 4×4: every pair's path | never a turn forbidden by the west-first rule; minimal length |
| S2-AM-MASK | `AdaptiveMinimal` with `escape_vc = 0`, `vcs_per_class = 2` | adaptive routes carry `vc_mask = 0b10`; the escape route `0b01` |
| S2-CDG-CYCLE | a four-router ring `Table` that turns in a cycle | refused; message lists the cycle's channels |
| S2-CDG-ESCAPE | the same table with `escape_vc` set | accepted; the escape VC's own graph checked acyclic |
| S2-CDG-SHIPPED | BFS tables on grid, torus, ring, star | all pass the check |
| S2-ORDERING | `ordering()` of every shipped algorithm | `Table`, `XY`: PAIR_PER_CLASS; `WestFirst`, `AdaptiveMinimal`: NONE |
| S2-REQUIRES | `requires_ordering = pair_per_class` with `WestFirst` | refused; with `XY` accepted; with `XY` and `vcs_per_class = 2` on a grid refused |

## 6. Step 3 — arbiter extension

| id | kind | assertion | expected |
|---|---|---|---|
| S3-FCFS | unit | `electIndex` over 1000 random candidate lists | the lowest `seq` every time |
| S3-RR | unit | `electIndex` with owners {0,1,2,3}, candidates present for a random subset each round | every owner with a candidate is served once per round; an owner without a candidate does not consume the rotation |
| S3-EQUIV | unit | 1000 random draws presented both as message buffers to `elect` and as the equivalent candidate list to `electIndex`, FCFS and RR | identical winner sequences |
| S3-STATE | unit | one `RRArbiter` used alternately through `elect` and `electIndex` | one shared rotation, observed through the winners |
| REG-* | sim | the regression suite, since the arbiter files changed | unchanged |

## 7. Step 4 — the engine on synthetic traffic

All `sim` on `NetworkOnly.csv` unless marked. Defaults: `flit_bytes` 16, `router_latency` 2,
`link_latency` 1, `credit_latency` 1, `vc_depth` 4, `vcs_per_class` 1, `num_classes` 3,
`local_ports` 0, seeded generators. "R" is routers traversed.

### 7.1 Conservation and determinism

| id | assertion | expected |
|---|---|---|
| S4-CONSERVE | after every run in this step | injected = ejected + in flight; no credit counter above its initial value; every reservation filled or held; totals in `NetworkStats.csv` |
| S4-DETERMINISM | every run in this step executed twice | byte-identical `NetworkStats.csv` and raw trace |
| S4-ASAN | the S4-UNLOADED and S4-CURVE runs under the ASan build on WSL | no report |

### 7.2 Unloaded timing

| id | assertion | expected |
|---|---|---|
| S4-UNLOADED | one packet, 1 and 5 flits, on 2×2 and 4×4 grids under `Table` and `XY`, torus 4×4, ring 5, star 5, corner to opposite corner | `R·2 + (R−1)·1 + (f−1)`: 2×2 → 8 and 12; 4×4 → 20 and 24; star → 2 and 6 |
| S4-INJECT-TICK | one packet, one router (`star`) | ejected `router_latency + (f−1)` ticks after injection, proving injection is visible in its own tick |
| S4-LATENCY-KEYS | `router_latency` 0 and 4, `link_latency` 1 and 3, on 4×4 corner to corner | the formula with those values |
| S4-CLASS-LAT | `class_link_latency = 2,5,5`, `vc_depth 8` (so credits never limit a 5-flit packet over a 5-tick link), one request and one response | formula with per-class link latency |

### 7.3 Flow control

| id | assertion | expected |
|---|---|---|
| S4-CREDIT-TPUT | a 200-flit stream over one link, `router_latency 0` (so heads behave like body flits), `vc_depth` 1, 2, 4, `credit_latency` 1 | throughput `min(1, vc_depth / (link_latency + credit_latency))` = 0.5, 1, 1 flits per tick within 0.06 |
| S4-CREDIT-LAT | the same with `credit_latency` 3, `vc_depth` 2 | `2 / 4` flits per tick |
| S4-WORMHOLE | `vc_depth` 2: a 5-flit packet P1 stalled at router 2 for 20 ticks by a full downstream; an unrelated 1-flit packet P2 that needs P1's VC at router 1 | P2 waits exactly 20 ticks more than unloaded |
| S4-VCT | the same with `vc_depth` 8 | P2 is not delayed |
| S4-CUTTHROUGH | a 5-flit packet over two links, trace enabled | the head's EXIT at router 1 precedes the tail's ENTER at router 1 |
| S4-TWO-IN-VC | two 3-flit packets back to back into one VC with `vc_depth` 6 | the second head is routed only after the first tail has left; both latencies match the formula plus the FIFO wait |
| S4-HALF-DUPLEX | opposite-direction flits on one connection, `half_duplex` 1 and 0 | never overlapping ticks with 1; overlapping with 0 |
| S4-INJECT-FULL | a generator at a rate the network cannot absorb, `inject_queue_depth` 2 | `pushMessage` returns `false` at least once, the generator's retry count is non-zero, and every packet is eventually ejected (no loss) |
| S4-EJECT-FULL | a sink that pops nothing for 50 ticks, `eject_queue_depth` 2 | the third packet's head is not allocated the local port; upstream VCs fill; after the sink resumes every packet is ejected, in order |
| S4-RESERVE | with S4-EJECT-FULL's setup | `canAcceptRX` is false while two reservations are held; `pushMessage2RX` is never refused |

### 7.4 Allocation and isolation

| id | assertion | expected |
|---|---|---|
| S4-VA-CONTEND | two heads at one router wanting the same output VC in the same tick | exactly one wins; the other waits one tick under FCFS with the lower `seq` first |
| S4-INTERLEAVE | two 5-flit packets on different VCs sharing one link, under `FCFSArbiter` and `RRArbiter` | FCFS: the older packet's flits all go first (ejected at unloaded and unloaded + 5, no interleaving); RR: the flits interleave (both ejected later than under FCFS on average) and still both by `unloaded + 5` |
| S4-ONE-PER-INPORT | two VCs at one input port both ready for different outputs | one flit per tick leaves that input port |
| S4-LOCAL-BW | two packets ejecting at one local port | one flit per tick at the local port |
| S4-CLASS-ISOLATION | the request-class sink stopped for 100 ticks while response traffic shares every link | response latencies unchanged from an unloaded run |
| S4-MULTICAST | one message with four destinations, `inject_queue_depth` 3 | refused until four slots are free; then four packets with consecutive `seq` in `to` order, all ejected |
| S4-SHARED-LOCAL | `local_ports = 1`, two generators on one router | injection alternates under FCFS by `seq`; ejection reaches the right generator every time |
| S4-RX-ORDER | prepared three-queue state at a sink, `rx_serving` `priority` and `alternate` | pop orders exactly as spec §7.4 |

### 7.5 Ordering and liveness

| id | assertion | expected |
|---|---|---|
| S4-ORDER | 1000 same-class packets per pair under random cross traffic, every shipped algorithm | in injection order for `Table` and `XY`; `XFAIL adaptive` for `WestFirst` and `AdaptiveMinimal`, which must show at least one reordering or the test is void |
| S4-SOAK | one million packets, hotspot pattern at 90 percent of the measured saturation rate, every shipped algorithm, 4×4 grid and torus | completes; conservation holds; no hang-dump trigger |

### 7.6 Fatal paths (`shell`)

| id | assertion | expected |
|---|---|---|
| S4-FATAL-DEST | a generator addressing an id that is not an endpoint | exit status 2; message names the destination and `seq` |
| S4-FATAL-MASK | a test routing function returning an empty `vc_mask` | exit status 2 |
| S4-FATAL-CREDIT | a debug hook that returns one credit too many | exit status 2; message names router, port, VC |
| S4-FATAL-RX | a debug hook making `pushMessage2RX` refuse | exit status 2 |
| S4-FATAL-STATUS | every fatal above | never exit status 0 |

### 7.7 Curves

| id | assertion | expected |
|---|---|---|
| S4-SAT-DEF | 4×4 `XY`, uniform random, 5-flit packets, offered rates stepped by 10 packets per 1000 ticks per endpoint, 50 000 packets per point, seeded | the **saturation rate** is the lowest offered rate at which the accepted rate (packets delivered per tick, per endpoint) falls below 0.9 of the offered rate; the generators are closed-loop (a refused packet is retried, not dropped) so latency alone plateaus and is not the criterion; the test records the rate in `results.csv` and asserts it lies between 0.4 and 0.7 of the per-port capacity of 200 packets per 1000 ticks (the published 4×4 mesh, one VC, 4-flit buffer figures; S8-BOOKSIM calibrates it) |
| S4-HOTSPOT | hotspot to one router | saturation rate strictly below S4-SAT-DEF's |
| S4-TRANSPOSE | transpose pattern under `XY` and `WestFirst` | `WestFirst` saturates no earlier than `XY` |
| S4-STATS | after S4-SAT-DEF | flits counted on link ports equal the sum over ejected packets of flits × links traversed (`totals,linkflits`); flits counted on local ports equal flits ejected |

## 8. Step 5 — system wiring

| id | kind | assertion | expected |
|---|---|---|---|
| S5-EEMBC-2x2 | sim | directory MSI on `MultiCoreSystem_Grid` (2×2, LLC on router 3, `num_classes 2` as the preset ships it, spec §6.3), all ten EEMBC traces | every run completes as the sweeps judge completion: own trace covered and footer written |
| S5-TILING | sim | the `[EVENT-PATH]` line of every S5 run | 100 percent tiled, zero `noresp` |
| S5-DETERMINISM | sim | every S5 run twice | byte-identical reports |
| S5-XPLAT | sim | `a2time01` on the grid, Windows and WSL | identical checksums of `LatencyReport_C*.csv` and `Summary.csv` |
| S5-ASAN | sim | `a2time01` on the grid under the ASan build | no report |
| S5-PLACEMENT | sim | LLC on router 0 versus router 3, unloaded single request from core 0 | request-bus column 2 and 8 ticks respectively, response 6 and 12, i.e. the worked example of spec §21 |
| S5-HANGDUMP | shell | the cyclic table of S2-CDG-CYCLE with the check disabled by a debug flag, a two-generator deadlock pattern, `OCTOPUS_HANG_DUMP` set | the run wedges, the dump prints a ring of held VCs with zero credits, exit status 2 |
| S5-HANGDUMP-POS | shell | the same table with the check enabled | refused at construction, so the dump is never needed |
| S5-RX-STRESS | sim | S5-EEMBC-2x2 with `eject_queue_depth = 2` and `num_classes = 2` | `XFAIL two-class` (forwards share the request class and can be held behind a full request RX); becomes a must-pass when §12 turns on three classes |
| S5-SPEED | sim | wall clock of S5-EEMBC-2x2 per trace, reported in `results.csv` | informational; flagged if above three times the legacy mesh run of the same trace |

## 9. Step 6 — degenerate equivalence

| id | kind | assertion | expected |
|---|---|---|---|
| S6-IDENTITY | sim | `MultiCoreSystem_NetNoC`: the legacy NoC matrix as a `custom` topology (implicit zero-latency endpoint routers, the switch an explicit router with `router_latency 1`), `Table`, `flit_bytes 0`, `vc_depth 32`, `num_classes 2`, `half_duplex 1`, `rx_serving alternate`, `link_model serial`, `class_link_latency 2,5` (spec §17.1), directory MSI, all ten EEMBC traces | `LatencyReport_C*.csv` and `Summary.csv` checksums equal REG-MESH's references |
| S6-FALLBACK | sim | only if S6-IDENTITY fails | per-core worst cases identical and finish cycles within one tick; the per-trace, per-core deltas of every Summary column are written to `test/network/out/s6/fallback.csv` and the decision to accept them is the user's (recorded in the implementation report) |
| S6-NOC-ALIAS | shell | after acceptance, `interconnect_type(s),NoC` | selects `Network` with the mapping of spec §17.2 and passes S6-IDENTITY |

## 10. Step 7 — observability

| id | kind | assertion | expected |
|---|---|---|---|
| S7-TRACE-HOPS | sim | raw trace of S5-EEMBC-2x2 `a2time01` | every packet has ENTER, SERVICE, EXIT per router traversed; hop count from the trace equals `NoC Hops` for every request |
| S7-TRACKER-SUM | sim | the same run | `NoC Wait + NoC Transfer` equals injection-to-ejection time for every request, both packets summed |
| S7-AHEAD | sim | the contention case of spec §21 built with two generators on the grid | `NoC Ahead = 4`, winner core 1; zero on the unloaded run |
| S7-CONVERT | shell | `octoviz.sh convert` on the run | zero mismatches, one lane per router in `occupancy.parquet` |
| S7-TIMEBASE | sim | router lane intervals against the report's bus columns after the converter's calibration | the first hop's ENTER is at `L1.ENTER + Request Bus start` and the last hop's EXIT at `REQ_BUS.EXIT`, within the one-cycle calibration the bus lanes already use |
| S7-VIEWER | manual | the selected request's route drawn across the lanes, screenshot filed under `test/network/ref/` | checked by eye once per release |

## 11. Step 8 — the BookSim comparison

| id | kind | assertion | expected |
|---|---|---|---|
| S8-BOOKSIM | sim | S4-SAT-DEF's configuration mirrored in a BookSim config file committed beside the test (`test/network/booksim/mesh44_dor.cfg`): 4×4 mesh, dimension-order routing, one VC, 4-flit buffers, 5-flit packets, uniform random, BookSim's three-cycle router (VA, SA, ST) and one-cycle links and credits; Octopus runs with `router_latency 3` and `credit_latency 4`, the mapping of BookSim's buffer residency (a slot is held until the flit has traversed the switch) onto the credit loop | the **saturation throughputs** (accepted flits per cycle per node at the first offered rate past saturation: BookSim's unstable point, Octopus's accepted < 0.9 offered) agree within 10 percent; BookSim 2 is built under WSL by the test (`test/network/out/booksim2`, bison and flex from `$HOME/local` when the distribution has none); if it cannot be built the test is XFAIL and prints Octopus's figure next to the published range |

## 12. Phase 2 additions

Recorded now so the XFAILs above have an owner:

| id | assertion | turns green |
|---|---|---|
| P2-THREE-CLASS | S5-RX-STRESS with `num_classes = 3` | must pass; S5-RX-STRESS loses its XFAIL |
| P2-ADMIT-RF | a controller with a full request queue still consumes a forward and a response | the named property of spec §14.2, on every shipped controller |
| P2-FSM-CLASS | every action in the directory FSM CSVs carries its message class annotation | the checker's input exists |
| P2-CHECKER | the two-class configuration with the directory protocol | refused by the protocol-level check, naming the cell |
| P2-BANKING | two LLC banks on routers 0 and 3, address-interleaved | EEMBC 10/10; every address reaches the same bank from every core |

## 13. Traceability

| spec section | tests |
|---|---|
| §3.1 contract | S4-INJECT-FULL, S4-EJECT-FULL, S4-RESERVE, S4-MULTICAST, S4-RX-ORDER |
| §4 configuration | S1-KEYS, S2-TABLE-BADPORT, S2-REQUIRES, S2-XY-TORUS-1VC |
| §5 topology | S1-GRID-*, S1-TORUS, S1-RING, S1-STAR, S1-CUSTOM-*, S1-ATTACH-* |
| §6 packets and classes | S1-FLITS, S1-CLS-* |
| §7 network interface | S4-INJECT-TICK, S4-INJECT-FULL, S4-EJECT-FULL, S4-RX-ORDER, S4-SHARED-LOCAL |
| §8 router | S4-TWO-IN-VC, S4-CUTTHROUGH, S4-LOCAL-BW, S4-ONE-PER-INPORT |
| §9 flow control | S4-CREDIT-*, S4-WORMHOLE, S4-VCT, S4-CONSERVE |
| §10 routing | S2-* |
| §11 allocation | S3-*, S4-VA-CONTEND, S4-INTERLEAVE |
| §13 timing | S4-UNLOADED, S4-INJECT-TICK, S4-LATENCY-KEYS, S4-CLASS-LAT |
| §14 ordering and deadlock | S2-CDG-*, S2-ORDERING, S4-ORDER, S4-SOAK, S4-CLASS-ISOLATION, S5-RX-STRESS, P2-* |
| §15 observability | S7-*, S4-STATS |
| §16 errors and determinism | S4-FATAL-*, S4-DETERMINISM, S5-DETERMINISM, S5-XPLAT |
| §17 degenerate configuration | S6-* |
| §18 generators | S4-SAT-DEF, S4-HOTSPOT, S4-TRANSPOSE |
