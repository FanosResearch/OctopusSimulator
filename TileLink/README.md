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

`cycles(i)` 0 runs until the system drains (reset complete, every agent idle including work it has planned,
no channel valid or mid-burst), bounded by `max_cycles(i)`. `cycles(i)` > 0 is a fixed observation window.
Negative values are refused. tlsim prints how the run stopped before any end-of-run report:

    tlsim: stop_reason=idle, complete=true, cycles=3387, limit=2000000
    tlsim: stop_reason=cycle_limit, complete=false, cycles=100, limit=100
    tlsim: incomplete: the cycle limit 100 was reached with work left: agent m0, agent s0
    tlsim: stop_reason=fixed_cycles, drained=false, cycles=500

A fixed window that ends with work in flight skips the end-of-run monitor rules (M-BURST-4, M-BURST-5,
M-RESP-7) and says so. `Kernel::runToDrain(max)` returns the same outcome (`RunResult`); `runUntilIdle`
keeps returning the cycles stepped.

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

The mask rules (M-MASK-1/2/3, A and B) check every byte lane of the bus, w up to 4096 (Table 4), and name the offending lanes as ranges (`lane(s) 63-64, 100`). The slave's `max_size` (M-ALIGN-3) and address region (M-ALIGN-5) come from the slave itself when it implements `SlaveLimits` (`RamSlave` and `ErrorSlave` do), or from `MonitorOptions`. `Monitor::finish(cycle)` applies the end-of-run rules M-BURST-4, M-BURST-5 and M-RESP-7. A request whose own burst is still in progress is excluded from M-RESP-7 (M-BURST-4 reports it). `violations()`, `count(rule)`, `byRule()` and `log()` are for tests. `summary()` gives `monitor L0: 2 violations (M-ALIGN-1: 1, M-RESP-7: 1)`. The monitor also pairs each request with its response and feeds `Stats::recordOperation` (the `operation` section of TLStats.csv: request first beat offered to response last beat accepted). The test fixture reports M-LEVEL-4 (a beat on a channel that a TL-UL/UH link does not have) through `offerOnMissingChannel`.

tlsim exit status: 0 clean, 1 usage, 2 a fatal error (a monitor violation in fatal mode, a watchdog trip, a refused configuration), 3 the monitor counted violations in report mode, 4 a run until drained reached `max_cycles` with work left (incomplete). Code 3 takes precedence over 4; both facts are printed. The summaries are printed and written to `<out>/monitor.txt`.

## Agents: RamSlave, ErrorSlave, FuzzMaster

`withAllAgents()` (`include/tilelink/agents/Registry.h`) registers them by name, together with the builtins and, when present, the topology agents of `RegistryTopology.h`. Example systems: `configs/system1_ul.csv` (FuzzMaster to RamSlave, TL-UL) and `configs/system2_uh.csv` (the same at TL-UH, with bursts, atomics, Intent and refresh; the full system 2 adds Xbar, WidthWidget and Fragmenter, see the file).

**RamSlave** (TL-UL/UH memory, plan §3.5) serves Get, PutFullData and PutPartialData. Atomics compute on the 2^size-byte little-endian integer (`tl::atomicOp`, `Atomic.h`): MIN, MAX, MINU, MAXU and ADD, plus XOR, OR, AND and SWAP per byte. Intent is answered with HintAck. Bursts work in both directions. A request executes when its last beat is accepted (§6.5). Its response is offered `latency` cycles later, at least 1. Data modes are `full` (sparse byte memory), `stamp` (one value per 8-byte group) and `none`. A corrupt write beat poisons the bytes it writes, per byte. A read returns a beat corrupt when any byte it carries is poisoned, and a clean write clears only the bytes it writes.

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
| `Fragmenter` | `max_size(i)` (required), `max_unsplit(i)` (default: upstream 2^z-1), `deny_corrupts_all(i)`=1, `depth(i)`=2 | splits Get/PutFull/PutPartial only. An unsplittable message (atomic, Intent, TL-C) above `max_size` is refused. The fragment index (last = 0) goes into the source bits above o, so downstream needs o+k. Reassembly in address order: denied = OR; a denied data response corrupt on every beat (§4.4), otherwise corrupt per beat as returned. **Limit:** a split write is not all-or-none (see below) |
| `SourceShrinker` | `pool(i)` (default 2^o downstream), `depth(i)`=2 | TL-UL/UH only; A ready LOW while the pool is empty |
| `Throttle` | `p_ready(d)`=0.5, `p_valid(d)`=0, `depth(i)`=2, `depth_A(i)`.. | random withholding from the agent's seeded RNG. valid is withheld only on beat 0 of a message not yet accepted, never mid-burst |

**Fragmenter support caveat.** A PutFullData or PutPartialData that the Fragmenter splits is not all-or-none. When a later fragment is denied, the earlier fragments' bytes are already written: the upstream response is denied, yet memory holds a partial effect (an 8-byte Put split in two 4-byte fragments, the second denied, leaves `17 17 17 17 0 0 0 0`). §4.4 says a denied message has no effect, so this is a gap, also present in rocket-chip's TLFragmenter (plan §3.5). ORing the fragments' `denied` is not a rollback, and retrying the write does not repair it. Use the Fragmenter for writes only where nothing downstream can deny a fragment. TL-ADAPT-ERR (`test_fragmenter`) pins the current behaviour.

Every agent declares `hints()` for the watchdog's default bound. The Throttle's latency is the run length exceeded with probability below 1e-9.

`ValueCheck` (`ValueCheck.h`, plan §3.6) is the admissible-set checker. `attach(link)` chains it onto the A and D `onAccept` hooks; it only observes. One checker may watch several links that share one memory. It works in data modes `full` and `stamp`. Its atomics are `tl::atomicOp` (`Atomic.h`), the one definition RamSlave and CoherentManager execute: arithmetic on the whole 2^size-byte operand (ADD carries through every byte; MIN/MAX signed, MINU/MAXU unsigned, compared from the top byte), logical ops per byte. An arithmetic result with an unknown input byte or a corrupt operand is unknown as a whole. In `stamp` mode a stamp names a write, not a number: a write's stamp replaces its 8-byte group and an atomic installs its operand's stamp, as RamSlave does. Reports go to `onViolation(ValueViolation)` (rule `V-READ` or `V-ATOMIC`, cycle, link, byte address, value, admissible set); the default is `fatal`. `counters()` gives writes, reads, atomics, bytes checked or skipped, denied and violations.

The tests are in `test/topology` (TL-TOPO), with their own scripted `Endpoint` and `MemSlave` (`TopoTest.h`). They include TL-ADAPT-ERR (`test_fragmenter`, `test_widthwidget`), the topology part of TL-ID (`test_shrinker`), and a 4 x 2 TL-FUZZ-U-style system over 10 seeds, with a mutant slave that ValueCheck must catch (`test_system`).

## CoherenceCheck

`include/tilelink/CoherenceCheck.h`, `src/check/CoherenceCheck.cpp` (plan §3.6, §6 TL-FUZZ-C, TL-UNCACHED). The TL-C system checker: a `Kernel::Observer` that watches the masters' TL-C links and three rules.

| rule | what it checks | when |
|---|---|---|
| `C-SWMR` | at most one master holds T on a block, and then no other master holds B (§9.1 Table 29) | a Grant[Data] with denied LOW is accepted by its master (first beat) |
| `C-GRANT` | the manager's view (§9.2 "should not issue a Grant ... if there is a pending ProbeAck"): a Grant toT is offered only while every other master is at N, a Grant toB only while no other master is at T, as last reported | every offer of a Grant[Data] with denied LOW (one report per message) |
| `C-VALUE` | a load returns, per byte, the value of the latest store reported before it (§6.5; plan §8 decision 8) | `loadPerformed` |

Permissions are learned from accepted beats only. A Grant[Data] sets the master's permission to its cap; its block is the block of the Acquire it answers, matched by source on the link. A ProbeAck[Data] or Release[Data] lowers the permission to its param's target (first beat accepted); it never raises it. A denied Grant changes nothing (§9.3.7). The data-value invariant comes from the agents: a `CacheMaster` reports at the hit, the manager when it executes an uncached Put/Atomic or answers an uncached Get, through `if (auto *c = CoherenceCheck::of(kernel())) c->loadPerformed(agent, addr, bytes, n, cycle)` (and `storePerformed`). The order is report order. A byte never stored admits any value, unless `setInitial(addr, bytes, n)` gave the initial memory image. Corruption (§4.4): a byte on a corrupt beat is never reported as a load, and a store of corrupt data, or an atomic's result from corrupt input, is reported with `storeUnknown(agent, addr, n, cycle)`, after which the byte admits any value until the next known store. The manager does this for its own copy and for an access it forwards to memory (N_u), whose response it relays corrupt per beat, by the bytes each beat carries.

Where it attaches: on the masters' own links, upstream of any Xbar. A link whose master is also a slave somewhere (an Xbar, a Buffer) can carry several masters, and the checker could not attribute a Grant or a ProbeAck to one of them. `attach(link)` therefore refuses such a link with a `ConfigError`, and it refuses a link below TL-C. `attachAll(kernel)` attaches every TL-C link whose master is a leaf. When the checker is registered as an observer and nothing was attached by hand, `onElaborate` does the same (`auto_attach`, default true); it also checks the leaf rule again on the complete system. It chains onto the A, C and D `onAccept` hooks and the D `onOffer` hook, so attach it after the Monitor and the ValueCheck of the same link: all of them keep reporting. A block is identified by its base address; `permission(link, address)` accepts any byte of the block and returns `N`, `B` or `T`.

Reports go to `onViolation(CoherenceViolation)` (rule, cycle, block, address, detail). The default is `fatal`: `TileLink: coherence C-SWMR: cycle=<n> block=<hex> addr=<hex>: <detail>`, exit 2. `counters()` gives grants, ProbeAcks, Releases, loads, stores, unknown stores, bytes checked and violations.

What it cannot show: it sees permissions as the links carry them, so a master that keeps using a copy after its ProbeAck, or writes under B, shows only through `C-VALUE`, and only for the accesses the agents report. Behind an Xbar it sees a Grant when the Xbar forwards it, after the manager issued it: a conservative view of the manager's decision, which cannot flag a legal Grant.

Tests: `test/coherence` (TL-COH: the C-SWMR, C-GRANT, denied-Grant and C-VALUE cases, report and fatal modes, the attach rules, and chaining with a Monitor and a ValueCheck on one link), `test/topology/test_forwarded` (TL-FORWARDED: corrupt data on the manager's forwarded path) and `test/negc` (TL-NEG-C: the 31 TL-C mutants of `docs/TileLink_Negative_Tests.md` §4 with the doc's controls, in report and fatal modes, and the Release-vs-GrantAck cases of `docs/TileLink_TLC_Tables.md` §11 item 5).

## TL-C agents and the policy engine

Plan §3.5 and §3.7 (step T6). The coherence policy is a pair of FSM tables in the Octopus CSV format, `tables/TLC_MESI_L1.csv` (the L1) and `tables/TLC_MESI_LLC.csv` (the inclusive manager), justified cell by cell in `docs/TileLink_TLC_Tables.md`. `tables/TLC_MSI_LLC.csv` is the MSI manager. The library reads the tables itself and executes them; no behaviour lives outside a table cell or an engine rule listed below.

| file | class | role |
|---|---|---|
| `coh/Table.h` | `coh::Table` | the FSM CSV reader. It reads the format exactly as Octopus's `FSMReader` does: sections `EventNum`/`ActionNum`/`StateNum`, rows and columns by position, cells `a1/a2/next`, `a/` = stay, bare `next`. Where `FSMReader` would silently misread, it refuses the table with a `ConfigError` naming the line and the row: a row out of position, an unknown name, a trailing blank line, a header column out of `EventNum` order |
| `coh/Policy.h` | `coh::L1Policy`, `coh::ManagerPolicy` | the policy engine, pure logic: the decoders (TLC_Tables §2.2, §3.2: message + line or directory state -> event, with the param-class collapsing and the probe counter's `last_` event), the action classification and the message builders (opcode, param per TLC_Tables §6, size = block, the copied fields of §7) |
| `agents/CacheMaster.h` | `CacheMaster` | the L1: sets x ways, LRU, a TL-C master port, a CPU side (`CpuSource`: built-in random, an Octopus trace, or a script) |
| `agents/CoherentManager.h` | `CoherentManager` | the inclusive manager: directory, LLC copy, inner TL-C slave ports (one per master, or one behind an Xbar), outer TL-UH master port; uncached accesses (plan §8 decision 8) and CMOs (plan §3.5) |
| `agents/TraceMaster.h` | `TraceReader`, `TraceMaster` | Octopus traces (`trace_C<n>.trc.shared`, parsed as `src/CPU.cpp` does). Mode `uncached`: the agent, Get/PutFullData on its own TL-UL/UH link. Mode `cpu`: the CacheMaster's `trace(s)` key. The CacheMaster owns the reader because a CPU side is not a TileLink link |

**MSI.** `TLC_MSI_LLC.csv` is the MESI manager with two cells changed and one state appended, so that no NtoB is ever granted toT. `I + AcquireBlock_NtoB` becomes `AddSharer/GrantData_toB/B_g`. `N + AcquireBlock_NtoB` becomes `Get_out/NB_d`. The new state `NB_d` (StateNum 26, last, as the row-order rule requires) is `NT_d` except that `AccessAckData` gives `SaveData/AddSharer/GrantData_toB/B_g`. The L1 table is shared by both protocols; under MSI it never enters E (TL-FUZZ-C checks this).

**Engine rules** (TLC_Tables §1.2, plus what T6 had to decide):
1. Each received channel has its own queue, always ready outside reset.
   - Per cycle, the first message of each queue whose cell is not `Stall` runs: at the L1, D then B; at the manager, E, outer D, C, then A. A `Stall` leaves the message parked and it is re-decoded every cycle. A parked B never blocks D, and a parked C (a Release that overtook its own GrantAck) never blocks E.
   - At the manager, inner A keeps per-block arrival order: once a message of a block stalls or waits for a resource, later messages of that block wait too.
2. Decode-fault deferral. "NtoB/NtoT from a listed holder" and "BtoT from the listed Tip" are faults (§3.2), except while the block has Probes outstanding. The sender's ProbeAck (C) may still be on its way while its next Acquire (A) has arrived, because A and C are not ordered. Every probing state Stalls inner A, so the message is parked and re-decoded once the ProbeAck has removed the sender.
3. Parking and answering. An inner-A cell that does not answer its request parks it on the block. An answering action (Grant*, AccessAckData_in, Execute, ForwardIn, HintAck*) answers the consumed inner-A message if the cell consumed one, otherwise the parked request. At the L1, a CPU access that leaves a stable state is parked on the line; Hit and Error serve the CPU access the cell consumed, otherwise the parked one.
4. Resources are reserved before the cell runs.
   - An Acquire is popped only with a free sink on its link (2^i), held until the GrantAck.
   - Acquire and Release sources come from separate A and C pools (2^o each); a ProbeAck copies the Probe's source.
   - Outer requests take a source from 2^o when they leave, in order.
   - A cell that allocates a line (I -> IS_a/IM_a at the L1, N -> NT_d at the manager) runs only with a free way. Otherwise a `Replacement` is raised on the LRU line whose Replacement cell can run, at most one eviction per set at a time at the manager.
5. Data.
   - A GrantData that is not denied fills the L1 line before the cell's actions run.
   - Corruption (§4.4) is kept per byte in every copy (`coh/BlockData.h`, in all data modes): a byte arriving on a corrupt beat is poisoned, a beat leaving is corrupt when any byte it carries is poisoned. Poison survives fills, ProbeAckData, ReleaseData, Grants to other masters and the manager's write-back. A clean CPU Store clears only the bytes it writes. A Load that reads a poisoned byte completes as an access error (`CpuOp::error`, counter `poisoned_loads`) and is not reported to the checkers as a value. An atomic with a poisoned old value or a corrupt operand returns every beat corrupt and poisons its result. Poisoned bytes are never reported to ValueCheck or CoherenceCheck as known values.
   - The LLC copy exists exactly in the states whose isDataValid is 1.
   - A `PutFullData_out` whose cell ends in a stable state (the eviction rows) is a port transaction: its AccessAck is consumed by the port (rule 7). Until that AccessAck, later outer requests to the block are held (H1).
6. Clients. Holders are identified by source range. A link whose master is an Xbar (through pass-through adapters such as Throttle and Buffer) has one client per Xbar upstream link, at `Xbar::sourceBase`. Any other link is one client. A Probe carries the client's base source.
7. Reports. Every performed Load and Store (the L1 Hit), and every uncached access at the cell that answers it, goes to `CoherenceCheck::of(kernel)`. A forwarded access (`N_u`) is reported at its outer response.
8. Stalls are reported once per parked message and state through `onTransition` (`Transition::stalled`), which the directed tests use.

**CacheMaster keys**:

| key | default | meaning |
|---|---|---|
| `table(s)` | `TLC_MESI_L1.csv` via `findTable` | the L1 table |
| `block_bytes(i)`, `sets(i)`, `ways(i)` | 64, 64, 4 | geometry |
| `cpu(s)` | `random`, or `trace` when `trace(s)` is given | `random`, `trace`, `script` (`pushOp`, tests) or `none` |
| `outstanding(i)` | 4 | accesses in flight; per block they run in program order |
| `ops(i)`, `addr_base(i)`, `addr_span(i)`, `store_pct(i)`, `evict_pct(i)`, `access_bytes(i)`, `rate(i)` | 1000, 0, 4096, 30, 0, 8, 100 | the random CPU |
| `trace(s)`, `timing(s)`, `max_gap(i)`, `max_ops(i)` | -, `asap`, 1000, 0 | the trace CPU; `timing trace` keeps the trace's cycle gaps, capped |
| `debug(i)` | 0 | print every executed cell |

**CoherentManager keys**:

| key | default | meaning |
|---|---|---|
| `protocol(s)` | `MESI` | `MESI` or `MSI` |
| `table(s)` | from `protocol` | overrides `protocol` |
| `block_bytes(i)`, `sets(i)`, `ways(i)` | 64, 1024, 8 | geometry |
| `addr_base(i)`, `addr_size(i)` | none | the region declared to the monitor |
| `debug(i)` | 0 | print every executed cell |

The default tables are found by `findTable`, in this order: `$TILELINK_TABLES`, then `TL_DEFAULT_TABLE_DIR` if compiled in, then `tables/` next to the executable or up to two levels above it, then `./tables` and `./TileLink/tables`, then the source tree (from `__FILE__`).

**Hints.**
- CacheMaster: is_master. Its latency is 4 hops, each bounded by the slowest declared latency among the non-cache agents: a Probe answered after a ReleaseAck round trip, which the manager may hold until a GrantAck.
- CoherentManager: latency of 8 such hops (one transaction, its eviction write included), queue 4 per client.

**Systems.** `configs/system3_tlc.csv` is 4 CacheMasters (random), an Xbar, a CoherentManager (8 sets x 2 ways, so it evicts with holders inside) and a RamSlave. `configs/system4_trace.csv` extends it with the EEMBC a2time01 traces, one per core; the paths are relative to `TileLink/` and every link has `a(i)=48` for the traces' 47-bit addresses. Both run clean in `tlsim` with the Monitor on every link.

**Tests.**

| test | contents |
|---|---|
| `test/coh` (TL-COH-ENGINE) | the reader, including every cell of the three tables against the doc's cell list and the MSI diff; the decoders and builders |
| `test/flows` (TL-FLOWS) | Figures 29 (both charts) and 30, and a Release overtaking its own GrantAck, as directed races. A `Gate` holds one channel of one link (`FlowTest.h`) and every chart line is asserted in order |
| `test/uncached` (TL-UNCACHED) | an uncached FuzzMaster beside 2 and 4 CacheMasters; no holder is left when an uncached write is performed; the decision-8 directed cases |
| `test/cmo` (TL-CMO) | the three CBOs x 1-3 holders x clean/dirty, racing Acquire and Release, denied outer write |
| `test/fuzzc` (TL-FUZZ-C) | 2/4/8 masters, MESI and MSI, direct and through an Xbar, 10 seeds each |
| `test/tlcsys` (TL-SYS-C) | systems 3 and 4 through `Kernel::build` |
| `test/denyc` (TL-DENY-C) | an ErrorSlave behind the manager and behind an outer Xbar |

`test/fixtures/TlcSystem.h` builds the fuzz systems: Throttles everywhere, a Monitor on every link, CoherenceCheck, ValueCheck on the outer link, GraphCheck, and the watchdog at its default bound.
