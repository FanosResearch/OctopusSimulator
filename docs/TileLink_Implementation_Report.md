# TileLink simulator component: implementation report

Running record of the implementation of `docs/TileLink_Plan.md`, step by step (plan §7). One section
per step: what was built, the decisions taken where the plan left room or turned out wrong, what the
tests measured, and the commit. Written on branch `tilelink`, off `network` at `9f845fa0`. Companion
documents: `docs/TileLink_TLC_Tables.md` (the TL-C tables, cell by cell), `docs/TileLink_Monitor_Rules.md`
(the monitor's 63 rules), `docs/TileLink_Negative_Tests.md` (the mutants).

## T0: the plan

`docs/TileLink_Plan.md`: a digest of the TileLink 1.9.3 specification (rules, messages, parameters,
the 16 errata found in it, §1.5), the component's architecture, the Octopus mapping, validation ids and
steps. Reviewed twice before code (once by a second model); the decisions of plan §8 were taken on
2026-10-21: the library in-tree, no same-cycle responses, a TileLink header in `Message`, MSI and MESI
only (MOESI to be revisited: TL-C has no sibling-L1 O state), 7 network classes with memory on the
network, Grant never toN, invalidate on uncached writes. Commit `83b4ffbc`.

## T1: the core library

**Built.** `TileLink/`, a C++17 CMake project with no Octopus dependency: types and encodings (every
opcode and param of plan §1.2-§1.3), `LinkParams`, `Msg` (constant wire address, derived beat
address, data modes none / stamp / full), `ChannelState` (handshake, burst lock, stats, hooks),
`Link`, `Agent`, `Kernel` (the three-phase cycle: offer, accept, commit; no combinational pass-through),
a reader of Octopus's CSV parameter format, the beat transcript, `TLStats.csv`, the `tlsim` driver.

**Measured.** TL-UNIT, 7 tests. **Commit** `1bccac06`.

## Test content for T2 and T4

`docs/TileLink_Monitor_Rules.md` (63 atomic rules, one row each), `docs/TileLink_Negative_Tests.md`
(65 mutants, the four TL-PROGRESS scenarios), `TileLink/test/golden/` (spec Figures 6-12, 17, 18 and 22
transcribed from the PDF pages, with "+1 cycle" variants where a figure draws a same-cycle response).
Two further spec errata found (Figure 10's third transaction, Figure 18's data). Commit `3f2c540e`.

## T5: the TL-C tables

`TileLink/tables/TLC_MESI_L1.csv` (10 states x 12 events) and `TLC_MESI_LLC.csv` (26 x 26), loaded by
Octopus's FSMReader to prove the format, justified cell by cell in `docs/TileLink_TLC_Tables.md`.
Departures from the plan, accepted: AcquirePerm unsupported (Octopus never issues it; TL-PERM deferred),
CBOClean probes only the Tip (a toB probe of a Branch is a no-op). Commit `1a695297`.

## T4: GraphCheck and the watchdog

GraphCheck refuses a cyclic agent graph at elaboration with the cycle's path; agents may declare
decoupled link groups (spec §5.1's DMA example). The watchdog keeps a timer per presented beat, per burst
and per received request against a declared `progress_bound`, and names the rule it points at (2, 3i,
3ii); a diagnostic, never a conformance verdict. Core hooks: kernel observers, `Agent::hints()`,
`Agent::decoupledGroups()`. **Measured.** TL-GRAPH (9 cases), TL-PROGRESS (the four §5.2 examples trip or
complete as the spec says). Interpretation recorded: "presented" is causal (offered unchanged since the
previous cycle). Commit `51df8e36`.

## T2 and T3: monitor, agents, topology, ValueCheck

**Built.** The Monitor (all 63 rules), RamSlave, ErrorSlave, FuzzMaster, scripted test fixtures; Xbar
(address, source and sink routing, ID remapping, rr / fixed / TDM arbitration holding a burst, C/D/E
independent of A/B), Buffer, WidthWidget, Fragmenter, SourceShrinker, Throttle; ValueCheck with the
admissible-set rule (acknowledgement order is not write order, plan §3.6).

**Found and fixed at integration.**
- The core's burst lock was moved by a beat interleaved into a burst and accepted in report mode;
  only the burst's own beats move it now (regression test in TL-UNIT).
- The Fragmenter's denied responses were corrupt "per beat as returned", which §4.4 forbids for a
  denied data response; corrupt on every beat is now the default, and the plan's text was corrected.
- The plan-§4 system 2 was added as a configuration and run as TL-FUZZ-U, with a `value_check(vs)` key
  in `tlsim` (one shared ValueCheck on the masters' links: an end-to-end check through the crossbar,
  the width converter and the fragmenter).

**Measured.** TL-WAVE 10/10 figures beat for beat; TL-NEG 33 UL/UH mutants (report and fatal mode);
TL-DET; TL-DENY; TL-TOPO 48 cases; TL-ADAPT-ERR; TL-ID; TL-FUZZ-U 10 seeds (about 64 KB of read data
checked per run, 0 violations on 8 monitored links). ctest 31/31. Commit `58b07e03`.

## Shared interfaces for T6, T7 and T9

Committed before the parallel work so that no two agents edit one header: `Message::tl` (channel 0 =
not TileLink; no existing decoder reads it), kind `K_GRANT_ACK` (22, also in octoviz and
`docs/Trace.md`), `txFreeSlotsCh` / `txReserveCh` / `txReleaseCh` on `CommunicationInterface`,
`MsgClass::tl_channel` and `count`, and the `CoherenceCheck` API. `run.sh reg` 6/6 unchanged. Commit
`f3a14d5e`.

## T6: TL-C in the library

**Checks** (commit `d9a824ac`). CoherenceCheck: C-SWMR at a Grant's accept, C-GRANT at its offer,
C-VALUE from agents' load and store reports; registered by `Kernel::build` (`coherence_check(i)`,
default 1). TL-NEG-C: the 31 TL-C mutants. One monitor change: an outstanding Intent no longer blocks
an Acquire or a Release (§9.2 names only pending Grant / GrantData / AccessAck / AccessAckData). A
Release overtaking its own GrantAck is legal on the wire; the same-cycle Release (M-CONC-4) and the
pre-Grant param (M-PERM-2) are flagged.

**Monitor fixes found by the fuzzing** (commit `5082c1eb`), all false positives on legal traffic:
- master-side M-CONC and M-PERM state per (block, client), the client traced from the source through
  Xbar ranges to the leaf master (§9.2 binds each master; a crossbar link carries several);
- C responses matched to the Probe of the same address and client (§6.4), not the oldest at the address;
- a ProbeAck / Release prune held from its offer and committed at its first beat, no longer at its last;
- an Acquire overtaken on a downstream link by a later prune (Figure 29) accepted within the window
  since the last Grant or ReleaseAck.

**Core** (commit `25e6e3a4`). `coh/Table` and `coh/Policy` (decode, action builders), CacheMaster,
CoherentManager, TraceMaster, `TLC_MSI_LLC.csv`. The tables doc's MSI recipe was wrong (it granted toB
to a BtoT, violating decision 7, and missed the cold-read path through `NT_d`); the MSI manager changes
two cells and appends `NB_d`. Engine rules beyond the doc's §1.2 are recorded there (§1.2 rule 1's
exception: an Acquire overtaking its own ProbeAck is decoded by its param while probes are outstanding).

**Measured.** TL-FLOWS (Figures 29, 30), TL-UNCACHED, TL-CMO (18 combinations), TL-DENY-C, TL-SYS-C
(system 4 runs all 275 304 a2time01 accesses), TL-FUZZ-C:

| configuration | ops | cycles | probes | monitor / coherence / value / watchdog |
|---|---:|---:|---:|---|
| MESI, 2 / 4 / 8 masters, direct links | 30k / 60k / 120k | 257k / 388k / 666k | 5.6k / 33k / 83k | 0 / 0 / 0 / 0 |
| MSI, 2 / 4 / 8 masters, direct links | 30k / 60k / 120k | 257k / 395k / 677k | 4.9k / 31k / 79k | 0 / 0 / 0 / 0 |
| MESI 4 and MSI 8 behind an Xbar | 60k / 120k | 561k / 1118k | 31k / 78k | 0 / 0 / 0 / 0 |

Under MSI no L1 ever entered E. ctest 41/41.

## T7: TileLink in Octopus

**Infrastructure** (commit `581b9b2d`). The library is built into Octopus. `TLPort`: a
`CommunicationInterface` on the Octopus side and a `tl::Agent` on one link; one receive queue per channel
served E, D, C, B, A; a message delivered at its last beat; per-channel transmit queues; the A and C
source pools and the Grant sink pool (`txFreeIdsCh` / `txReserveIdsCh` / `txReleaseIdsCh`); Octopus
fields restored on the far side; B fanned out over `Message::to`. `TileLinkInterconnect`: the kernel,
crossbar, ports and monitors, one TileLink cycle per core cycle, one clock per kernel enforced at
elaboration. `interconnect_type TileLink` in `MultiCoreSystem_Mesh`. Admission:
`CoherenceProtocolHandler::predictEmissions` (default false, so the existing protocols keep their
three-queue `txNeed` unchanged) and `tlAllocatesId`. Logger: TileLink bus events; the Probe and
Completion columns appear only with a TileLink interconnect.

**Handlers, banks, memory, C-PERM** (commit after this section is written, see below).
- `TLC_L1` and `TLC_LLC` run the library's tables and policy directly, under `CacheControllerDirectory`.
  Clients by `Message::from`; the parked request is also in `m_pending_requests`; one new action
  `TL_RESPOND`; `CacheController::stall()` force-pushes TileLink messages.
- Banked LLCs: the Xbar's interleaved route form `link:interleave:<shift>:<count>:<index>` (the formula of
  `homeOf`), built by TileLinkInterconnect from the banks' ids; the one-bank refusal is lifted.
- CoherenceCheck was not attached inside Octopus (the kernel is built by hand); it is now
  (`tl_coherence_check(i)`, default 1), and a new rule, **C-PERM** (Table 29), checks every access a cache
  serves from its copy against the permission on its link: a load needs B or T, a store T. The checker's
  permission is never below the master's own, so a report is never a false alarm. It is the check that
  catches a hit without permission, which no link shows and which trace runs (no data) cannot otherwise
  see. CacheMaster reports to it as well.
- `tx_depth_B` below the number of caches is refused at elaboration (a probe fan-out reserves one B slot
  per cache; a shallower queue would never admit it).

**Measured.**
- TL-OCT-A: EEMBC, 10 benchmarks, MESI and MSI, 1 and 2 banks, crossbar rr / fixed / TDM and the LLC's
  RRArbiter: 160/160 runs with 4/4 cores finished, 0 faults, 0 monitor violations, `NET overtakes 0, MC
  overtakes 0`, event-path tiling fail=0, and (rerun with C-PERM) 0 coherence violations. a2time01:
  60 567 grants, 165 250 load hits and 49 487 store hits checked.
- TL-OCT-EVICT: 4 KiB LLC, queues of 1-2, `pwb_size` 1, MESI and MSI, 1 and 2 banks: 40/40; replacements
  = LLC write-backs = memory writes in every run.
- TL-OCT-CMP (TLC MESI on the crossbar vs directory MESI v2 on the bus): per-core request counts
  identical 10/10; DRAM reads within 0.9% 10/10; **L1 misses 9-18% lower under TileLink, outside the
  plan's 2%** (XFAIL). Cause, measured: about 72% of TileLink misses are coherence transfers, whose count
  follows the interleaving; a slower transfer lets the holder hit more per tenure. Raising only the
  directory LLC's array latency 10 -> 20 cycles brings its a2time01 misses from 67 253 to 61 286, TileLink's
  level (60 567). C-PERM rules out hits without permission. Mean miss latency (a2time01) 99.1 vs 80.2
  cycles: the probe round trip and the fourth hop inside LLC service, the GrantAck window in the LLC
  stall, 8-beat GrantData on the 8-byte bus. The criterion is left for a decision (plan §6).

## T8: memory over TileLink

`TLMemSlave` (the memory controller's interface on a TL-UH slave link: checks each request against the
LLC-memory contract, writes the D header of the controllers' untagged responses from the request they
answer) and `TLMemMaster` (the LLC bank's memory side, with the hazard hooks). Memory links go through a
second crossbar: the crossbar routes A by address only, and the same line must reach the LLC from an L1
and memory from the LLC; separate crossbars also keep the outer A/D off every inner queue, so the agent
graph stays acyclic. `memory_on_network(i) 1` with `interconnect_type TileLink` now means memory over
TileLink; MCsim per controller with the controller-select bits removed, as on the Network. Presets
`MultiCoreSystem_TileLinkM` and `MultiCoreSystem_TileLink2B2M`.

**Found.** ASan: `MCsimInterface` copied a 64-byte block from its 8-byte read index (stack over-read; the
compiler had warned). Fixed in its own commit `03a28a7c`; outputs md5-identical.

**Measured.** TL-MEM unit tests (round trip on both memory models, H2 forwarding inside MCsim,
back-pressure with queues of 1, 2 controllers interleaved with the counts matching `memOf`, a live
hazard), TL-SYS-MEM, TL-OCT-MEM-BASIC and -MCSIM (EEMBC on the memory presets).

## T9: the Network carries TileLink (network part)

`num_classes` 5 and 7 keyed on the TileLink channel (7: A_in < B < C < A_out < D_out < D_in < E; "out" =
both ends memory-side, from `interconnect.memory_ids(vi)` or `Network::setMemoryEndpoints`); every
mismatch fatal; per-channel admission in the NI; `ProtocolCheck` around `Network::analyzeProtocols`, with
a second verdict line listing every self-raised cell that emits (the fill-triggered write-back that the
class order does not order and the resource argument of plan §5.3 covers). TrafficGenerator `tl_mix`
mode. **Measured.** Step TL (unit, TL-NET-5, TL-NET-7, TL-NET-7-TINY, TL-NET-DET, negatives); `run.sh all`
140 PASS, 9 XFAIL, 0 FAIL (was 120/9/0). Commit `824a21d7`.

## T9b: the TL-C handlers over the Network

The handlers and tables are unchanged; where each crossbar-port function lives on the Network:

| function | on the Network | why |
|---|---|---|
| whole-message delivery | the NI, unchanged | it delivers at the tail flit |
| sender stamp (`Message::from`) | the NI, unchanged | it stamps every push; the LLC's clients come from it |
| B fan-out over `Message::to` | the NI's all-or-none multicast, unchanged | the fan-out the admission reserves for |
| A / C source pools, Grant sink pool, `txFreeIdsCh` | `TLNetPort` (a TLPort subclass between controller and NI) | TileLink link semantics; the NI of every Octopus preset stays as it was |
| the §6.4 namespace remap, routing checks | `TLNetPort` | the handlers see what they see on the crossbar |

The NI gained one optional hook (`setDeliverHook`, unset on every Octopus network). The library Monitor
and CoherenceCheck run at message level on a never-stepped boundary kernel (one link per endpoint, w =
block; the NoC as an Xbar, plus a TL-UH Xbar for the outer links): every rule a one-beat message can
break is checked; the ordering inside the NoC and beats are not. C-PERM runs on the L1 links: a
permission rises when the Grant enters the L1's receive queue and falls when its ProbeAck or Release
enters its inject queue, so a report is never a false alarm. `inject_queue_depth` below the number of
caches is refused. With memory on the network the system calls `setMemoryEndpoints` (memory ports and
controllers) before the controllers, `checkProtocols` and the first message.

**Measured.** TL-OCT-NET 32/32 batches (4 presets x MESI/MSI x FCFS/RR x plain/stress, 320 EEMBC runs):
every run 4/4 cores, 0 faults, 0 refused packets, hazard 0/0, tiling 0 failures (82 245 056 requests),
monitor 0, coherence 0 (C-PERM 19 468 506 grants, 47 012 279 load and 15 764 271 store hits). TL-OCT-EVICT
on the network presets 8/8 (replacements = write-backs = memory writes). TL-NET-7 on Grid2BMTL and
Grid2B2MTL: every class carries traffic (e.g. Grid2BMTL A_in 59 916, B 59 438, C 66 571, A_out 7 824,
D_out 7 824, D_in 67 049, E 59 915) and the reservations balance. checkProtocols: 5 classes, edges A->B
A->D B->C C->D D->E; 7 classes, edges A_in->B A_in->A_out A_in->D_in B->C C->A_out C->D_in A_out->D_out
D_out->D_in D_in->E; consumption order holds on both, with the Replacement cells listed as the
self-consumed emitters. Negative: a broken class order, a too-shallow inject queue and untagged tables on a
TileLink network are refused. `test/tilelink/run.sh net` 50/50.

## Review problem 1: admission refused for a full MSHR leaked transmit reservations (2026-10-23)

Fixed on `network` (`0f5b4f0c`, present since P2-ADMISSION `869f46c0`), carried here (`3a39532f`): the
structural MSHR check runs before `txAdmit`, so a refused admission reserves nothing. TL-ADMIT-MSHR (the
real TLC_L1, one MSHR, two lines: the A slots and sources stay at their baseline through 50 refused
retries; 2 slots leaked on the old ordering) and the `mshr` step (a2time01 with 1 and 2 L1 MSHRs on the
crossbar, 5 and 7 classes: 6/6, all of which wedged before). Commit `5fe2164a`.

## Review problem 3: retry after a denied Grant (2026-10-23)

**Policy** (the user's): a CPU access whose Acquire meets a denied Grant stays outstanding and is retried;
the CPU is answered once, by the Grant that succeeds (docs/TileLink_Denied_Grant_Retry_Plan.md). The
standalone CacheMaster keeps completing it with an access error; the tables are unchanged.

**Built.**
- TLC_L1's Error action no longer fabricates a zero-data response: after the cell's GrantAck and state
  update, `TL_RETRY_PENDING` (a new controller action, appended to the enum) replays the line's parked
  access. It stays parked: its pending entry is the MSHR credit, its Store bytes stay where they were
  saved. The replay copy enters the processing queue ahead of every younger access to its line, so the
  queue's per-line gate keeps them behind it (a younger access must not issue its own Acquire in the
  stable-state window after the denial), is held until the next controller cycle, and keeps its place
  when admission refuses it. Its Acquire cell does not park it a second time. It decodes in the line's
  current state, so a Probe that took S away makes the retry NtoT.
- `CoherenceProtocolHandler::isCpuDemand` (default `Message::isDemandRequest`; TLC_L1: every CPU Load
  and Store, also a Store carrying its bytes) drives the MSHR check and, through
  `FRFCFS_Buffer::setOrdered`, the per-line gate. The other protocols keep the default.
- TLC_LLC keeps a transient line tracked without a valid copy (`N_g`: see TLC_Tables s11.1 item 12).
- Test hook: `OCTOPUS_TL_DENY_FILLS=<n>` makes each LLC take its first n outer fills as denied, the table's
  own `NT_d -> Grant_denied/N_g`.
- Logger: a request with more than one memory round trip (the denied fill, then the retry's) takes its
  memory milestones and LLC service from the last trip; Total includes the failed attempt, which falls in
  the LLC stall. Requests with one trip are unaffected.

**Measured.** TL-RETRY (12 unit tests on the real TLC_L1 and, for E-BLOCKED's second part, the real
TLC_LLC): LOAD, GRANTDATA, STORE-MISS, UPGRADE, PROBE, E-BLOCKED (the denied Grant waits for an E slot;
at the manager an Acquire arriving in N_g stalls while the GrantAck is consumed), A-BLOCKED, ORDER (a
younger same-line Store stays behind the retried Load; another line and a Probe progress), REPEATED,
PERMANENT (20 denials: no response, one parked access, bounded), MSHR, ARRAY. All 12 fail on the old
Error action (a premature CPU response; in ORDER the younger Store issued first) and pass with the
change. TL-RETRY-SYS: a2time01 with 200 denials per LLC on the crossbar, 5 and 7 classes, MESI and MSI:
6/6, every access answered once, monitor 0, coherence 0, hazard 0/0, tiling 0 failures.

**Found, not fixed (pre-existing, reported).**
- With the occupancy data-array model and an L1 latency above 0, a TLC Store miss loses its bytes to the
  fill, denial or not (TL-RETRY-ARRAY checks that the retry changes nothing there; the pipelined model is
  correct). The shipped presets set no L1 latency.
- A demand refused by admission is re-queued at the back of the processing queue
  (BaseController::processLogic), where a younger access to the same line from the same core can
  overtake it through the per-line gate. The replay keeps its place; the general case is open.

## Review problem 2: corruption lost through caches and at the Octopus boundary (2026-10-23)

**Standalone library: per-byte poison.**
- `coh/BlockData.h` keeps a poison flag per byte, in every data mode (None too). Import: a byte written
  from a beat takes that beat's corrupt flag. Export: a beat is corrupt when any byte it carries is
  poisoned (ORed into the existing flags, so a denied data response stays corrupt on every beat).
- CacheMaster: a clean Store clears poison only on the bytes it writes. A Load that reads a poisoned byte
  completes as an access error (`CpuOp::error`, counters `errors` and `poisoned_loads`) and is not
  reported to CoherenceCheck as a value.
- CoherentManager: poisoned bytes are excluded from the value reports (Put, atomic, AccessAckData_in). An
  atomic with a poisoned old value or a corrupt request beat returns every beat corrupt and poisons its
  result bytes; the operation is still performed, not turned into a denial.
- RamSlave: poison is per byte (it was per 8-byte group, and a smaller clean write erased the group's
  poison).
- Measured: TL-POISON (5 cases, 122 checks, all fail on the old code): GrantData corrupt on exactly the
  beat that carries a poisoned RAM byte, at w = 4, 8, 16, Full and Stamp; the poisoned Load is an error,
  a clean range of the same block is not; poison survives ReleaseData, the manager's copy, a Grant to a
  second master and the manager's write-back to RAM; a 1-byte clean Store clears only that byte in the
  cache; a 1-byte clean PutPartialData clears only that byte in RAM.
- Limit, from §4.4: corrupt is per beat on the wire, so a write-back marks the whole beat and RAM poisons
  every byte of it. The byte precision that survives a write-back is the cache's, not the memory's.

**Octopus: refused at the boundary.**
- `TLPort::deliver` refuses a beat with corrupt set, before the message is converted to an Octopus
  Message: exit 2, naming the port, cycle, message and beat. The guard does not depend on `tl_monitor`.
- A denied data response is exempt, because §4.4 requires its beats corrupt and its data is never used.
  The send side now marks such a message corrupt on every beat (crossbar and Network ports). Octopus's
  own denied Grant (`Grant_denied` at TLC_LLC) is dataless, so this exemption is reached only from the
  unit fixture.
- The Network port presents a dataless copy of each message to its monitor only. It cannot carry or
  discover corruption, so Network support is clean data only. No Octopus memory model produces
  corruption.
- Measured: TL-PORT-FATAL-CORRUPT (a fault marks one beat of an 8-beat AccessAckData corrupt on the
  wire: monitor fatal, off and report, beat 0 and beat 3: 5/5 exit 2, beat named, nothing delivered),
  TL-PORT-CORRUPT-DENIED (a denied AccessAckData is delivered, denied, monitor 0). TL-RETRY-SYS still
  6/6 after the Network change.
- Not built, by decision (plan §8 decision 10): corruption inside Octopus messages, caches, MSHRs or
  memory models.

## T10: documentation and the regression

`SUPPORTED_CONFIGURATIONS.md` (TL-C protocols, the TileLink crossbar and network presets, the standalone
component), `docs/NoC_Architecture_Spec.md` (5 and 7 classes, `memory_ids`, the TileLink annotations in the
protocol check, the self-consumed emitters line, the `reserve` rows, `tl_mix`, the NI delivery hook), this
report. Regression: `REG-TILELINK` in `test/network/run.sh reg` runs a2time01 on `MultiCoreSystem_TileLinkM`
(crossbar, memory over TileLink) and `MultiCoreSystem_Grid2BMTL` (network, 7 classes) and requires
byte-identical Summary / LatencyReport against `test/network/ref/reg-tilelink-{xbar,net}.md5` plus 0 monitor
and 0 coherence violations; references regenerated alone with `run.sh tilelink-refs` (two generations
identical). `run.sh reg` 7/7. Not done: the LaTeX manual chapter (no TeX on the build machine).

