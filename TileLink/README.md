# TileLink simulator component

A cycle-level, beat-accurate simulator of the TileLink 1.9.3 protocol (TL-UL, TL-UH, TL-C), written
against `docs/TileLink_Plan.md`. It is a standalone C++17 library with no Octopus dependency
(namespace `octopus::tl`); Octopus adds it with `add_subdirectory(TileLink)`. This is step T1: the
library skeleton (types, encodings, parameters, messages, channels, links, kernel, config reader,
transcript, statistics), the `tlsim` driver and the TL-UNIT tests. Agents, the monitor and the
checkers come with T2 and later.

## API overview (include/tilelink/)

| header | class | role |
|---|---|---|
| Types.h | `Channel`, `Level` | the five channels (A < B < C < D < E) and the three conformance levels |
| Types.h | `OpA..OpE`, `Arith`, `Logic`, `Intent`, `Cap`, `Grow`, `Prune`, `Report` | opcode and param encodings (Tables 13, 23, 25, 27, 31) |
| Types.h | `opcodeName`, `hasData`, `isRequest`, `responseOf`, `legalOn`, `paramLegal`, `beatCount`, `aligned` | the encoding rules as pure functions |
| Types.h | `fatal`, `ConfigError` | the single fatal path ("TileLink: msg", exit 2) and the refused-configuration exception |
| Params.h | `LinkParams` | w, a, z, o, i and the level of a link; `validate()` with exact messages |
| Msg.h | `MsgHeader`, `Msg`, `MsgPtr`, `DataMode` | one message: constant control fields, per-beat mask/data/corrupt, simulator timestamps; `beats()`, `beatAddress(k)`, `deriveMask()` |
| Channel.h | `ChannelState`, `Beat`, `ChannelStats` | the wire of one channel of one link: offered beat, valid, ready, the burst lock, counters, observer hooks (`onOffer`, `onAccept`, `onViolation`) |
| Link.h | `Link` | master to slave: its parameters and the `ChannelState`s it has (A+D or A..E) |
| Agent.h | `Agent`, `Role`, `LinkRole` | the abstract agent: `capability()`, the three phase hooks `offer`/`accept`/`commit`, `reset`, `idle`, a seeded `rng()` |
| Agent.h | `NullMaster`, `NullSlave`, `AgentRegistry` | the trivial pair, and the type-name registry `tlsim` builds systems from |
| Kernel.h | `Kernel`, `KernelOptions` | owns agents and links; the three-phase cycle; reset; `build()` from a config; `run`, `runUntilIdle`; transcript and stats |
| Config.h | `Config`, `Param` | reader of the Octopus CSV parameter format (`name(type),value`, `name[k].key`, wildcards, `Extends`, `-p` overrides) |
| Transcript.h | `Transcript` | one line per accepted beat: `cycle link ch opcode param size source sink address beat mask data corrupt denied` |
| Stats.h | `Stats`, `LatencyStats` | TLStats.csv: per-channel beats/busy/stall, per-opcode latencies, outstanding-ID histogram |

Timing contract (plan §3.2): offer, then accept, then commit, once per cycle; an offer at cycle t is
computed from state at the start of t, so there is no combinational pass-through and no same-cycle
response. Burst-lock and reset violations go to `ChannelState::onViolation` (default: `fatal`).

## Build and test

    cmake -S TileLink -B build-tl && cmake --build build-tl -j && ctest --test-dir build-tl --output-on-failure

## Run

    build-tl/tlsim -c TileLink/test/unit/null_system.csv -o out --trace -p cycles(i)=200

writes `out/TLStats.csv` and `out/transcript.txt`. Flags match `Octopus_Simulator`: `-c <config.csv>`,
`-o <dir>`, `--trace`, `-p name(type)=value`.

## GraphCheck and Watchdog

The system-level checkers of plan §3.6. Both are `Kernel::Observer`s (`onElaborate`, `onCycleEnd`) that `Kernel::build` registers from the config.

| key | default | meaning |
|---|---|---|
| `graph_check(i)` | 1 | rule 1 of §5 at elaboration: the agent graph (§5.3, one edge per link, master to slave) has no cycle |
| `agent[k].decoupled_groups(s)` | none | §5.1: links the agent drives independently, as separate vertices, e.g. `L_ctrl;L_dma` (`+` joins links into one group) |
| `watchdog(i)` | 1 | per-message timers for rules 2, 3i and 3ii |
| `progress_bound(i)` | formula | the declared bound in cycles; absent: (slowest latency + deepest queue + largest burst) x masters, from `Agent::hints()`; 0: watchdog off |
| `watchdog_mode(s)` | `fatal` | `fatal` dumps and exits 2; `report` logs one line per trip and continues (`Watchdog::trips()`, `onTrip`) |

A cycle is refused at elaboration with its path, `A -> B -> A (links: L1, L2)` (exit 2). `GraphCheck::topologicalOrder()` and `dot()` export the graph. The watchdog prints its bound and the formula that produced it once, at elaboration. A trip names the rule, the link, the channel, the agent the rule binds, the message and the timer. The dump then lists every unanswered request, every offered beat and every burst in progress, so that a reader can apply rule 3's exemption ("unless a higher-priority message is in flight or unanswered").

A trip means that a timer outlived the declared bound. It is not a conformance verdict: a finite timer cannot tell an illegal permanent stall from a legal long delay (§4.3), so a conforming system with a bound that is too small also trips (TL-PROGRESS shows this). "Presented" is operational. A beat counts from the first cycle it is offered with its current control and data, and lowering valid or changing the beat resets it, so an agent that alternates valid accumulates no rule 3i timer. Its burst trips rule 2 instead, or the request it owes trips rule 3ii. There is no global "no beat anywhere" timer: one starved request behind busy links would escape it. The watchdog never recovers.

## Monitor

`include/tilelink/Monitor.h`, `src/check/Monitor.cpp`. One `Monitor` per link (plan §3.6) implements the rules of `docs/TileLink_Monitor_Rules.md`, each cited in the code by its id and spec section. It attaches to the channel hooks of its link: `onOffer` (static field rules, M-BURST), `onAccept` (matching, IDs, concurrency, permissions) and `onViolation`, so the core's burst-lock and reset reports are counted by the monitor instead of exiting. It chains any hooks already installed (ValueCheck, for example) and restores them when it is destroyed. A monitor must be destroyed before the kernel that owns its link. The header lists every rule as implemented, tested in T2, or exercised only in T6 (TL-C: M-CONC, M-PERM and the B/C/E parts of the other groups, which run from the same per-block state).

Every violation is one line:

    <rule-id> <spec §> link=<name> cycle=<n> ch=<A..E> msg=<opcode>[param=<p> size=<s> src=<o> sink=<i> addr=<hex> beat=<k>]: <text>

| key (tlsim) | default | meaning |
|---|---|---|
| `monitor(i)` | 1 | a monitor on every link |
| `monitor_mode(s)` | `fatal` | `fatal` exits 2 on the first violation; `report` counts per rule id and continues |
| `block_bytes(i)` | 64 | TL-C block size (M-CONC, M-PERM) |
| `allow_acquire_under_acquire(i)` | 0 | M-CONC-1 relaxation |
| `c_ordered(i)` | 1 | M-CONC-11 |
| `standalone(i)` | 1 | M-RESET-2: refuse `reset_cycles` < 100 (a system of only NullMaster/NullSlave is exempt) |

The slave's `max_size` (M-ALIGN-3) and address region (M-ALIGN-5) come from the slave itself when it implements `SlaveLimits` (`RamSlave` and `ErrorSlave` do), or from `MonitorOptions`. `Monitor::finish(cycle)` applies the end-of-run rules M-BURST-4, M-BURST-5 and M-RESP-7. A request whose own burst is still in progress is excluded from M-RESP-7 (M-BURST-4 reports it). `violations()`, `count(rule)`, `byRule()` and `log()` are for tests. `summary()` gives `monitor L0: 2 violations (M-ALIGN-1: 1, M-RESP-7: 1)`. The monitor also pairs each request with its response and feeds `Stats::recordOperation` (the `operation` section of TLStats.csv: request first beat offered to response last beat accepted). The test fixture reports M-LEVEL-4 (a beat on a channel that a TL-UL/UH link does not have) through `offerOnMissingChannel`.

tlsim exit status: 0 clean, 1 usage, 2 a fatal error (a monitor violation in fatal mode, a watchdog trip, a refused configuration), 3 the monitor counted violations in report mode. The summaries are printed and written to `<out>/monitor.txt`.

## Agents: RamSlave, ErrorSlave, FuzzMaster

`withAllAgents()` (`include/tilelink/agents/Registry.h`) registers them by name, together with the builtins and, when present, the topology agents of `RegistryTopology.h`. Example systems: `configs/system1_ul.csv` (FuzzMaster to RamSlave, TL-UL) and `configs/system2_uh.csv` (the same at TL-UH, with bursts, atomics, Intent and refresh; the full system 2 adds Xbar, WidthWidget and Fragmenter, see the file).

**RamSlave** (TL-UL/UH memory, plan §3.5) serves Get, PutFullData and PutPartialData. Atomics compute on the 2^size-byte little-endian integer: MIN, MAX, MINU, MAXU and ADD, plus XOR, OR, AND and SWAP per byte. Intent is answered with HintAck. Bursts work in both directions. A request executes when its last beat is accepted (§6.5). Its response is offered `latency` cycles later, at least 1. Data modes are `full` (sparse byte memory), `stamp` (one value per 8-byte group) and `none`. A corrupt write beat poisons its 8-byte groups, and a later read returns that beat corrupt.

| key | default | meaning |
|---|---|---|
| `latency(i)`, `latency_jitter(i)` | 1, 0 | service latency, plus uniform jitter from the agent's rng |
| `latencies(vi)` | none | per-request latency list, in request order |
| `queue_depth(i)` | 8 | requests held; a_ready LOW when full |
| `fifo(i)` | 0 | responses in request order (§6.3 FIFO domain) |
| `max_size(i)` | 2^z - 1 | declared maximum (M-ALIGN-3) |
| `addr_base(i)`, `addr_size(i)` | none | declared region (M-ALIGN-5); the FuzzMaster's default address set |
| `deny(vi)` | none | base,size pairs: denied, no effect, data beats corrupt (§4.4) |
| `refresh_period(i)`, `refresh_cycles(i)` | 0, 0 | a_ready and d_valid LOW for refresh_cycles of every refresh_period (§5.2 example 2) |
| `level(s)` | UH | declared capability (UL or UH) |

`hints()` returns latency = the slowest service time + jitter + refresh, and queue = queue_depth x the largest response burst, for the watchdog's default bound.

**ErrorSlave** is a RamSlave that denies everything: each request gets its normal response type with `denied` HIGH and no effect, and every beat of a data response is corrupt.

**FuzzMaster** generates random legal TL-UL/UH traffic, deterministic from its seeded rng. Every message is legal for the monitor: opcode at the level, legal param, aligned, within the slave's max_size and region, derived mask (PutPartialData a random subset of its lanes), a unique source from a pool of min(2^o, `sources`), one burst at a time. Before a message's first accept it may be retracted or replaced. Counters `issued()`, `answered()`, `deniedResponses()`, `deniedDataBeats()` and `deniedCorruptBeats()` are for tests. Each cycle it records its outstanding count into the outstanding-ID histogram of TLStats.csv.

| key | default | meaning |
|---|---|---|
| `level(s)` | the link's | UL or UH |
| `ops(i)` | 1000 | requests in total, then idle |
| `w_get`, `w_put`, `w_putpartial`, `w_arith`, `w_logic`, `w_intent` (i) | 4, 3, 1, 1, 1, 1 | op mix (atomics and Intent only at UH) |
| `addr_base(i)`, `addr_span(i)`, `align(i)` | slave region, 1 | address set |
| `size_min(i)`, `size_max(i)`, `atomic_size_max(i)` | 0, (UL: log2 w; UH: min(2^z - 1, slave max, 6)), 3 | size range |
| `sources(i)` | 2^o (max 1024) | source pool |
| `rate(i)` | 70 | % chance per idle cycle of a new request |
| `fence_every(i)` | 0 | wait for all responses every N requests |
| `retract_pct(i)` | 0 | % chance per cycle of retracting/replacing a not-yet-accepted message |
| `d_ready_pct(i)` | 100 | % chance per cycle of d_ready HIGH |

Test fixtures (`test/fixtures/`, compiled into the tests, not the library): `ScriptedMaster` and `ScriptedSlave` offer scripted beats, explicit `(cycle, channel, message, beat)` entries or adaptive "send from cycle c until accepted" ones. They take per-cycle ready schedules, and the slave can answer requests by itself with a response that the test may tweak. TL-WAVE, TL-NEG, TL-DET, TL-DENY and TL-FUZZ-U-lite are under `test/wave`, `test/neg`, `test/det`, `test/deny` and `test/fuzzlite`.

## Topology agents and ValueCheck

Plan §3.5 adapters, each an `Agent` with links sorted by role (upstream: it is the slave; downstream: it is the master), registered by `registerTopologyAgents(reg)` (`agents/RegistryTopology.h`) under the type names below. Every path adds exactly one cycle when its stage is empty (plan §3.2), every channel has its own stage (a C/D/E ready never depends on A/B, §4.1), and a refused configuration throws `ConfigError` at elaboration. Integers accept `0x` hex.

| type | keys (`agent[k].*`) | notes |
|---|---|---|
| `Xbar` | `routes(vs)` `link:base:size` entries (no overlap, §6.3), `default(s)` link, `arbitration(s)` `rr`/`fixed`/`tdm`, `tdm_slot(i)`=1, `depth(i)`=2 | N x M, one width and level. A/C by address, B/D by source, E by sink (Table 14). Sources packed per upstream link into one downstream namespace, sinks per downstream link into one upstream one (§6.4): downstream o and upstream i must hold the totals. The grant is held for a whole burst |
| `Buffer` | `depth(i)`=2, `depth_A(i)`..`depth_E(i)` | depth 1 = one beat per two cycles; depth 0 refused (plan §3.2: connect directly) |
| `WidthWidget` | `depth(i)`=2 | any w to any w, every level. Merge: mask concatenated, corrupt OR. Split: mask sliced, corrupt copied. A dataless message gets its mask re-derived; a sub-bus D is placed by its request's address, remembered by source |
| `Fragmenter` | `max_size(i)` (required), `max_unsplit(i)` (default: upstream 2^z-1), `deny_corrupts_all(i)`=1, `depth(i)`=2 | splits Get/PutFull/PutPartial only. An unsplittable message (atomic, Intent, TL-C) above `max_size` is refused. The fragment index (last = 0) goes into the source bits above o, so downstream needs o+k. Reassembly in address order: denied = OR; a denied data response corrupt on every beat (§4.4), otherwise corrupt per beat as returned |
| `SourceShrinker` | `pool(i)` (default 2^o downstream), `depth(i)`=2 | TL-UL/UH only; A ready LOW while the pool is empty |
| `Throttle` | `p_ready(d)`=0.5, `p_valid(d)`=0, `depth(i)`=2, `depth_A(i)`.. | random withholding from the agent's seeded RNG. valid is withheld only on beat 0 of a message not yet accepted, never mid-burst |

Every agent declares `hints()` for the watchdog's default bound. The Throttle's latency is the run length exceeded with probability below 1e-9.

`ValueCheck` (`ValueCheck.h`, plan §3.6) is the admissible-set checker. `attach(link)` chains it onto the A and D `onAccept` hooks; it only observes. One checker may watch several links that share one memory. It works in data modes `full` and `stamp` (the stamp is the byte value). Reports go to `onViolation(ValueViolation)` (rule `V-READ` or `V-ATOMIC`, cycle, link, byte address, value, admissible set); the default is `fatal`. `counters()` gives writes, reads, atomics, bytes checked or skipped, denied and violations.

The tests are in `test/topology` (TL-TOPO), with their own scripted `Endpoint` and `MemSlave` (`TopoTest.h`). They include TL-ADAPT-ERR (`test_fragmenter`, `test_widthwidget`), the topology part of TL-ID (`test_shrinker`), and a 4 x 2 TL-FUZZ-U-style system over 10 seeds, with a mutant slave that ValueCheck must catch (`test_system`).

## CoherenceCheck

`include/tilelink/CoherenceCheck.h`, `src/check/CoherenceCheck.cpp` (plan §3.6, §6 TL-FUZZ-C, TL-UNCACHED). The TL-C system checker: a `Kernel::Observer` that watches the masters' TL-C links and three rules.

| rule | what it checks | when |
|---|---|---|
| `C-SWMR` | at most one master holds T on a block, and then no other master holds B (§9.1 Table 29) | a Grant[Data] with denied LOW is accepted by its master (first beat) |
| `C-GRANT` | the manager's view (§9.2 "should not issue a Grant ... if there is a pending ProbeAck"): a Grant toT is offered only while every other master is at N, a Grant toB only while no other master is at T, as last reported | every offer of a Grant[Data] with denied LOW (one report per message) |
| `C-VALUE` | a load returns, per byte, the value of the latest store reported before it (§6.5; plan §8 decision 8) | `loadPerformed` |

Permissions are learned from accepted beats only. A Grant[Data] sets the master's permission to its cap; its block is the block of the Acquire it answers, matched by source on the link. A ProbeAck[Data] or Release[Data] lowers the permission to its param's target (first beat accepted); it never raises it. A denied Grant changes nothing (§9.3.7). The data-value invariant comes from the agents: a `CacheMaster` reports at the hit, the manager when it executes an uncached Put/Atomic or answers an uncached Get, through `if (auto *c = CoherenceCheck::of(kernel())) c->loadPerformed(agent, addr, bytes, n, cycle)` (and `storePerformed`). The order is report order. A byte never stored admits any value, unless `setInitial(addr, bytes, n)` gave the initial memory image.

Where it attaches: on the masters' own links, upstream of any Xbar. A link whose master is also a slave somewhere (an Xbar, a Buffer) can carry several masters, and the checker could not attribute a Grant or a ProbeAck to one of them. `attach(link)` therefore refuses such a link with a `ConfigError`, and it refuses a link below TL-C. `attachAll(kernel)` attaches every TL-C link whose master is a leaf. When the checker is registered as an observer and nothing was attached by hand, `onElaborate` does the same (`auto_attach`, default true); it also checks the leaf rule again on the complete system. It chains onto the A, C and D `onAccept` hooks and the D `onOffer` hook, so attach it after the Monitor and the ValueCheck of the same link: all of them keep reporting. A block is identified by its base address; `permission(link, address)` accepts any byte of the block and returns `N`, `B` or `T`.

Reports go to `onViolation(CoherenceViolation)` (rule, cycle, block, address, detail). The default is `fatal`: `TileLink: coherence C-SWMR: cycle=<n> block=<hex> addr=<hex>: <detail>`, exit 2. `counters()` gives grants, ProbeAcks, Releases, loads, stores, bytes checked and violations.

What it cannot show: it sees permissions as the links carry them, so a master that keeps using a copy after its ProbeAck, or writes under B, shows only through `C-VALUE`, and only for the accesses the agents report. Behind an Xbar it sees a Grant when the Xbar forwards it, after the manager issued it: a conservative view of the manager's decision, which cannot flag a legal Grant.

Tests: `test/coherence` (TL-COH: the C-SWMR, C-GRANT, denied-Grant and C-VALUE cases, report and fatal modes, the attach rules, and chaining with a Monitor and a ValueCheck on one link) and `test/negc` (TL-NEG-C: the 31 TL-C mutants of `docs/TileLink_Negative_Tests.md` §4 with the doc's controls, in report and fatal modes, and the Release-vs-GrantAck cases of `docs/TileLink_TLC_Tables.md` §11 item 5).
