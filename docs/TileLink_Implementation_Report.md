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

**Not yet done.** TileLink over the Network presets (plan §5.3 I-C) with the TL-C handlers: on the Network
the network interface stands where TLPort stands on the crossbar (source and sink ids, B fan-out, the
sender's stamp), and the 7-class presets must call `setMemoryEndpoints` before `checkProtocols`.

## Spec text pending for `docs/NoC_Architecture_Spec.md`

T9 drafted the additions for §6.3 (5 and 7 classes), §14.3 (TileLink annotations, the self-consumed
emitters line), §15.4 (the `reserve` rows), §18 (`tl_mix`) and §4 (`memory_ids`); they are applied in
T10 together with the user documentation.
