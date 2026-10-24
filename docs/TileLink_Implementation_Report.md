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

## Review problem 4: atomics, the checker against the agents (2026-10-23)

**Cause.** ValueCheck computed an arithmetic atomic wider than 8 bytes per 8-byte group (a 16-byte ADD
lost its carry out of byte 7; MIN/MAX compared each half alone), and in Stamp mode it did arithmetic on
stamps (stamp 10 ADD operand stamp 5 expected 15; RamSlave stores 5). The agents were right.

**Built.**
- `include/tilelink/Atomic.h`: `atomicLess`, `atomicOp`, `applyAtomic`, pure functions on little-endian
  byte vectors. RamSlave, CoherentManager and ValueCheck all call it; the two agents' identical local
  copies are removed.
- ValueCheck Full: one `applyAtomic` on the whole operand. An unknown old byte or a corrupt operand beat
  makes the whole arithmetic result unknown; a logical result is unknown per byte.
- ValueCheck Stamp: a write enters every byte of each 8-byte group it touches (the last write in address
  order wins within a group, as RamSlave); an atomic installs its operand's stamp; its returned old stamp
  is checked as a read. Acknowledgement and denial visit exactly the bytes a write entered (`Op::written`).

**Measured.** TL-ATOMIC (8 cases, 57 checks): the helper against literal vectors (16- and 32-byte ADD
carries and wraparound; signed and unsigned MIN/MAX decided by the sign byte and by byte 9; logical ops
and SWAP); RamSlave end to end at w = 4, 8, 16 (the review's 16-byte ADD over two beats; signed MIN then
MAXU); the review's stamp 10 / operand 5 case at w = 4, 8; a 4-byte stamp write read back from the other
half of its group; the CoherentManager's Execute cell (an LLC copy with no holder) running a 16-byte
Put, ADD, MIN and MAXU at w = 4, 8, 16 with no outer write, ValueCheck on the uncached link agreeing. The
old checker fails the end-to-end cases (1, 8 and 4 violations). `test_valuecheck`
`wide_atomic_wrong_old_value_still_rejected`: a wrong 16-byte old value is still reported. Library 45/45.

## Review problem 5: mask checks stopped at lane 64 (2026-10-23)

**Cause.** The M-MASK rules packed the mask into a 64-bit integer, so on a bus wider than 64 bytes a
missing or extra lane at 64 or above was never reported.

**Built.** The monitor walks all w lanes into lane lists, A and B alike. The diagnostic formats the lists
as ranges (the first 16 runs, then a count). It no longer decides which lanes are checked.

**Measured.** TL-MASK (7 cases, 70 checks): a missing active lane at 63, 64, 100 and the last lane, at
w = 8, 64, 128, 4096 (M-MASK-2, the lane named, nothing else reported); an extra inactive lane (M-MASK-1);
a fault on beat 1; legal full, partial and zero PutPartialData masks (no report); a PutPartialData lane
outside its region (M-MASK-3, not M-MASK-1); a ProbeBlock on B at w = 128 with lane 100 HIGH and lane
63 LOW; fatal mode in a child process (exit 2, M-MASK-2 and lane 100 named). The cases above lane 63
fail on the old monitor. Library 45/45.

## Review problem 6: a run that hit its cycle limit reported success (2026-10-23)

**Cause.** `Kernel::runUntilIdle` returned the cycles stepped, whether the system drained or the budget ran
out, and tlsim exited 0 either way (a reset-only budget of 100 cycles "passed" with no traffic). A negative
`max_cycles` became a huge unsigned limit.

**Built.**
- `Kernel::runToDrain(max)` returns a `RunResult` (cycles stepped, Idle or CycleLimit). `drained()` is the
  predicate: reset complete, every agent idle, no channel valid or mid-burst. It is checked after every
  step, so a run that drains on its last allowed cycle is Idle. A zero budget steps nothing and reports
  the state as it is. `busy()` lists the agents and links still active. `runUntilIdle` is a wrapper.
- tlsim: negative `cycles` or `max_cycles` refused (exit 2); the `stop_reason` line printed before the
  end-of-run rules; an incomplete run lists what is busy and exits 4; report-mode violations stay exit 3;
  a fixed window that did not drain skips the end-of-run rules and says so.

**Measured.** TL-RUN (6 cases, 22 checks, tlsim as a subprocess, its exit status and TLStats.csv): the
review's 100-cycle budget exits 4 with `stop_reason=cycle_limit` and the busy agents, with and without
the monitor; a drained run exits 0; a 500-cycle window prints `fixed_cycles` and exits 0; a 150-cycle
limit in report mode exits 3 with the stop line and the M-RESP-7 reports; the drained run's own cycle
count as the limit exits 0, one less exits 4; negative limits exit 2. The old tlsim exits 0 with no stop
reason on the 100-cycle budget; on `max_cycles=-1` it was still running after 13 minutes and was stopped. `test_kernel`
`run_to_drain_reports_how_it_ended`: drained on the last cycle, one short, early, planned future work,
zero budget, an unfinished reset. TL-NEG's exit-status case now runs until drained (a fixed window no longer
applies M-RESP-7). Library 45/45.

## Shared controller defects and review follow-up (2026-10-23)

Plan: docs/Shared_Controller_Defects_Plan.md and docs/TileLink_Review_Followup.md. Order agreed with the user:
issue 3 with R1, issue 2, R2, issue 1. Issues 3 and 2 are fixed on `network` first and carried here.

**Issue 3 and R1: a refused demand keeps its place** (`network` e67a9e7c, here 0ccb1a3c).
- A CPU demand refused by admission went to the back of the processing queue, where a younger access to its
  line passed it through the per-line gate (an older Load returned a younger Store's value). It now goes back
  to its own place, is held for the rest of the controller cycle and the scan continues: other lines' hits and
  the completion traffic that frees the resource still run, the line's younger demands stay behind it. A
  refused non-demand message still ends the cycle in place. A refused pop is not progress for the hang dump.
- The hold is keyed by msg_id and applies only while the message is a CPU demand
  (`CoherenceProtocolHandler::isCpuDemand`), so completion traffic sharing the request's msg_id stays eligible.
  The denied-Grant replay hold uses the same predicate now (it used the lower-interface source).
- R1, introduced here by 3df508a4: TLC_L1 counted a Store carrying its bytes as a demand for the MSHR check, but
  the refusal path classified it with `Message::isDemandRequest`, restored it in place and ended the cycle; the
  dataless Grant queued behind it was never processed. Both paths now use `isCpuDemand`.
- Tests: P2-ADMIT-ORDER and P2-ADMIT-ORDER-LLC (network unit: the older request issues first at an L1 and at an
  LLC, hit-under-miss, an InvAck sharing a held GetS's msg_id completes its eviction); TL-RETRY-MSHR-STORE and
  TL-RETRY-MSHR-UPGRADE (R1: a denied and a successful dataless Grant behind a refused Store, both Stores
  complete, bytes read back); TL-ADMIT-ORDER (the older Load returns 0x11, not the younger Store's 0x77). Each
  fails on the code before the fix.

**Issue 2: a Store's bytes belong to the array access that serves it** (`network` 002aa522, here 09bf2360).
- With the occupancy array and an L1 latency above 0 a Store miss's merge was refused by the busy array after
  the fill, and the refusal was ignored; a Store hit during another request's fill lost its write the same way.
  Reproduced for TLC, directory MSI and directory MESI.
- The controller records the transition's array access (the first action that claims the array inline or
  parks it). A Store's write of the same request and line rides on it when inline (the fill and its merge are
  one access) and is appended to it when parked. Any other write waits for its own access. At an L1 a Store hit
  that carries its bytes claims the array, so its answer, its commit and its write happen together. A refused
  write is fatal; the saved bytes are dropped only once written.
- Tests: DATA-STORE-MISS and DATA-STORE-HIT (directory MSI and MESI; occupancy 0 and 4, pipelined 4; offsets 0,
  8, 48; untouched bytes; the eviction's write-back), TL-RETRY-ARRAY (now requires the Store's bytes in both
  models, with and without the denial), TL-DATA-STORE-HIT. Each fails on the code before the fix.
- Trace runs carry no Store bytes: the 30 MEM-OFF runs and the crossbar TileLink run are byte-identical to the
  issue 3 results.
- Not covered: a CPU Store of a whole line (64 bytes) takes the full-line fill path and exits on an absent line.

**R2: corrupt data on the manager's forwarded path** (175f8204, library only).
- `CoherenceCheck::storeUnknown`: the bytes admit any value until the next known store.
- `reportForwarded`: a byte on a corrupt response beat is not compared, the clean beats still are; a corrupt
  Put's bytes and an atomic's result from corrupt input become unknown. ForwardIn relays corruption per beat by
  the bytes each beat carries (it marked every beat corrupt). The manager's own copy marks its poisoned Put bytes
  and atomic results unknown too.
- Tests: TL-FORWARDED (4 cases; three fail on the old code; the unflagged wrong value is still C-VALUE).
  Library 46/46.

**Issue 1: the Network runner and missing Python packages** (`network` f31b751d and c3519a2e, here 20f0b886
and e2c33a14).
- numpy and duckdb are probed once with the steps' interpreter (`$PY`, which a caller may now set) and reported
  with their versions. Each check that needs them is XFAIL with an install hint when they are missing, and only
  that check; with them present a validator or converter failure stays FAIL. The home map of P2-BANKING and the
  controller map of MEM-MULTI are their own results (P2-BANKING-MAP, MEM-MULTI-MAP). An EXIT trap reports a run
  that stops early (`RUN ABORTED before completion`); a complete run ends with `(run complete)`.
- Measured with interpreter wrappers that block a package (nothing uninstalled): both present, all: 121 PASS,
  10 XFAIL; both missing, all: the run completes, 111 PASS, 20 XFAIL; duckdb missing, steps 7 and M4: the
  numpy checks PASS, the duckdb ones XFAIL; a converter forced to fail: S7-CONVERT and S7-VIEWER-API FAIL; the
  old stepM.sh without numpy: `RUN ABORTED before completion (status 1)`. Each full run's two FAILs are the
  reference checks below.
- Validation environment on this machine: the system python3 3.12.3 has neither package; the runs use a
  virtual environment with numpy 2.5.4 and duckdb 1.5.6 (`PY=<venv>/bin/python3` or that venv first on PATH).

**Runner: a successful reference generation is complete** (`network` 9d1824b4, here a3ed2c8d). `refs` and
`tilelink-refs` exited before the completion flag and printed `RUN ABORTED (status 0)`; they are marked complete
once their generator has returned, and a usage error is not an abort. `test/network/selftest.sh` checks every mode
with stubs (8/8 here; the old runner fails REFS and USAGE).

**Whole-line CPU Store** (`network` be5f59a2, here c9bbaa86).
- An aligned 64-byte CPU Store was taken for fill data by `CacheController::updateCacheLine` (its size, with the
  transient state's valid flag): a miss wrote an absent line and the run exited with status 0 ("update data of
  an unfound line") before the GetM; an S upgrade wrote the line at the Store, before the GetM was granted.
  Directory MSI, directory MESI and snoop MSI, every array model. TLC_L1 was not affected.
- At an L1 a lower-interface message is a CPU access (`cpuAccess`: the L1 role and the source; not the size, not
  the kind, not a CPU port); its update is a state update, the bytes are written at the Store's hit. A fill with
  no line to install into exits 2 with a diagnostic.
- Tests run as child processes that must exit 0 and print a completion marker: DATA-LINE-MISS, -HIT, -UPGRADE
  (3 protocols x 3 array models, nonuniform patterns; the upgrade's line keeps its old bytes and the Store is
  unanswered before any response and while an acknowledgement or the own GetM is pending), -LLC-WRITEBACK, -API
  (ExternalCPU's default-size WRITE on the gem5 preset), -FATAL (nonzero status, diagnostic); TL-DATA-LINE (the TLC
  control). MISS, HIT, UPGRADE, API and FATAL fail on the old code.
- Trace output: `network` all on be5f59a2 gives the same verdicts as the reference commit (123 PASS, 0 FAIL): no
  reference-checked output changed. Not covered: a Store that crosses a line boundary.
- Found, not fixed: `test/network/run.sh` resolves the simulator path before it builds, so on a fresh worktree
  (no binary yet) every simulation of that first run fails and `refs` writes the hash of empty output without
  complaint. The first of three reference generations here hit it and was discarded; the committed references
  come from the two that agree. Proposed: resolve the binaries after `build`, and refuse a reference whose run
  produced no Summary.csv.

**Regression references: measured differences, updated 2026-10-24 with the user's approval** (issue 3 changes
timing; issue 2 and R2 change none). Regenerated, and only these: `reg-memnet.md5`, the nine EEMBC hashes of
`mem-off/Grid2B` and of `mem-off/Grid` that changed, and `reg-tilelink-xbar.md5` (20 files here, the first 19 on
`network` e52ed82e). Unchanged and untouched: every Directory hash, both mi_pingpong hashes, `reg-tilelink-net.md5`,
the legacy mesh, bus and TDM references. Two generations agree; with them `network` all and this branch's
`network` all pass with no FAIL.

| Check | Before | After | Difference |
|---|---|---|---|
| REG-MEMNET (Grid2BM, a2time01) | finish 1001680, mean of core averages 9.095, worst total 422 | 1001619, 9.088, 437 | finish -0.01%, average -0.07% |
| MEM-OFF, Grid2B (10 EEMBC) | reference | 9 of 10 differ | finish -0.05% to +0.14%, average -0.30% to +0.33% |
| MEM-OFF, Grid (10 EEMBC) | reference | 9 of 10 differ | finish -0.36% to +0.37%, average -2.24% (pntrch01) to +0.99% |
| MEM-OFF, Directory (bus) | reference | 10 of 10 identical | none |
| REG-TILELINK, crossbar (TileLinkM) | finish 996714, average 9.032, worst total 918 | 997660, 9.042, 674 | finish +0.09%, average +0.11% |
| REG-TILELINK, network (Grid2BMTL) | reference | identical | none |

Per benchmark (MEM-OFF; finish = the last core's finish cycle, average = the mean of the four cores' average
latency, worst total = the largest worst-case total latency; before = `network` 0f5b4f0c, after = e67a9e7c):

| Preset | Benchmark | Finish before | Finish after | Finish change | Average before | Average after | Average change | Worst total before | Worst total after |
|---|---|---|---|---|---|---|---|---|---|
| Grid2B | a2time01 | 985608 | 985390 | -0.02% | 8.8593 | 8.8459 | -0.15% | 410 | 428 |
| Grid2B | aifirf01 | 1023969 | 1023965 | -0.00% | 8.6732 | 8.6731 | -0.00% | 417 | 417 |
| Grid2B | basefp01 | 1067254 | 1066736 | -0.05% | 9.3456 | 9.3174 | -0.30% | 413 | 422 |
| Grid2B | cacheb01 | 1493111 | 1493323 | +0.01% | 9.8977 | 9.8989 | +0.01% | 432 | 437 |
| Grid2B | empty | 654004 | 654908 | +0.14% | 7.5347 | 7.5597 | +0.33% | 404 | 407 |
| Grid2B | iirflt01 | 1018865 | 1018528 | -0.03% | 9.1152 | 9.1081 | -0.08% | 427 | 399 |
| Grid2B | mi_pingpong | 348 | identical | | 77.8214 | identical | | 292 | identical |
| Grid2B | pntrch01 | 1318208 | 1318236 | +0.00% | 8.7414 | 8.7393 | -0.02% | 452 | 450 |
| Grid2B | rspeed01 | 1768321 | 1768407 | +0.00% | 8.6876 | 8.6870 | -0.01% | 418 | 407 |
| Grid2B | ttsprk01 | 2512975 | 2514403 | +0.06% | 9.3256 | 9.3011 | -0.26% | 430 | 422 |
| Grid | a2time01 | 970455 | 968998 | -0.15% | 8.6071 | 8.5889 | -0.21% | 559 | 468 |
| Grid | aifirf01 | 1024000 | 1020354 | -0.36% | 8.6131 | 8.5985 | -0.17% | 563 | 471 |
| Grid | basefp01 | 1065796 | 1067326 | +0.14% | 9.3040 | 9.3123 | +0.09% | 500 | 520 |
| Grid | cacheb01 | 1495498 | 1501011 | +0.37% | 9.9329 | 10.0315 | +0.99% | 571 | 560 |
| Grid | empty | 652172 | 652873 | +0.11% | 7.3544 | 7.3772 | +0.31% | 531 | 450 |
| Grid | iirflt01 | 1016744 | 1020043 | +0.32% | 9.0798 | 9.1214 | +0.46% | 469 | 456 |
| Grid | mi_pingpong | 351 | identical | | 57.1032 | identical | | 295 | identical |
| Grid | pntrch01 | 1314362 | 1310270 | -0.31% | 8.6728 | 8.4788 | -2.24% | 566 | 617 |
| Grid | rspeed01 | 1766553 | 1764776 | -0.10% | 8.6999 | 8.6761 | -0.27% | 555 | 493 |
| Grid | ttsprk01 | 2506596 | 2507434 | +0.03% | 9.1814 | 9.1954 | +0.15% | 543 | 614 |

mi_pingpong is identical everywhere. The refusals happen at the LLC banks (Grid2BM a2time01: 157 refused pops
before, 297 after, since held demands keep their place and the scan reaches more messages). Every other check
of `network` all and of the TileLink runner is unchanged.

## T10: documentation and the regression

*Historical (2026-10-22, before the review fixes): the reference and `reg 7/7` results below describe that
checkpoint. The current references and results are in "Shared controller defects and review follow-up".*

`SUPPORTED_CONFIGURATIONS.md` (TL-C protocols, the TileLink crossbar and network presets, the standalone
component), `docs/NoC_Architecture_Spec.md` (5 and 7 classes, `memory_ids`, the TileLink annotations in the
protocol check, the self-consumed emitters line, the `reserve` rows, `tl_mix`, the NI delivery hook), this
report. Regression: `REG-TILELINK` in `test/network/run.sh reg` runs a2time01 on `MultiCoreSystem_TileLinkM`
(crossbar, memory over TileLink) and `MultiCoreSystem_Grid2BMTL` (network, 7 classes) and requires
byte-identical Summary / LatencyReport against `test/network/ref/reg-tilelink-{xbar,net}.md5` plus 0 monitor
and 0 coherence violations; references regenerated alone with `run.sh tilelink-refs` (two generations
identical). `run.sh reg` 7/7. Not done: the LaTeX manual chapter (no TeX on the build machine).

