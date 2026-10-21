# TileLink simulator component — plan

Status: T0 done 2026-10-21: reviewed (two review rounds, one by Codex), the §8 decisions taken, no code
yet. Source: *TileLink Specification 1.9.3* (SiFive, 95 pages), read in full. Written against branch
`network` at `9f845fa0`; the work goes on branch `tilelink`. Section numbers "§x" below are the spec's;
"plan §x" are this document's.

Goal: a TileLink simulator that runs on its own (its own executable, traffic generators, checker) and that
Octopus can use as its interconnect protocol: L1s, LLC banks and memory controllers talking TileLink, over a
crossbar or over the existing Network engine.

## 1. What the spec requires (the rules the model and the checker implement)

### 1.1 Structure

| item | rule |
|---|---|
| conformance levels | TL-UL: Get, Put, single-beat only. TL-UH: + Atomics, Intent (hints and CMOs), bursts. TL-C: + Acquire, Probe, Release on channels B, C, E |
| agent, link | an agent owns one or more links; a link is master→slave; a link is A+D (TL-UL/UH) or A–E (TL-C) |
| channels | A master→slave requests; B slave→master requests to cached blocks; C master→slave responses to B, plus voluntary Release; D slave→master responses (and Grant, which is also a request); E master→slave GrantAck |
| priority | A < B < C < D < E, refined per agent in §5.1 (for each channel: the receiving side ranks below the sending side) |
| per-link parameters (Table 4) | w data bytes (1–4096, power of 2); a address bits (0–128); z size-field bits (1–4); o source bits (0–64); i sink bits (0–64) |
| clock, reset | both ends of a link share one clock; valid held LOW for ≥100 cycles after reset; crossings need adapters |

### 1.2 Messages (Tables 12, 13, 30)

| channel | opcode | message | level | data | response |
|---|---|---|---|---|---|
| A / B | 0 | PutFullData | UL | yes | AccessAck |
| A / B | 1 | PutPartialData | UL | yes | AccessAck |
| A / B | 2 | ArithmeticData | UH | yes | AccessAckData |
| A / B | 3 | LogicalData | UH | yes | AccessAckData |
| A / B | 4 | Get | UL | no | AccessAckData |
| A / B | 5 | Intent | UH | no | HintAck |
| A | 6 | AcquireBlock | C | no | Grant or GrantData |
| A | 7 | AcquirePerm | C | no | Grant |
| B | 6 | ProbeBlock | C | no | ProbeAck or ProbeAckData |
| B | 7 | ProbePerm | C | no | ProbeAck |
| C / D | 0 | AccessAck | UL | no | — |
| C / D | 1 | AccessAckData | UL | yes | — |
| C / D | 2 | HintAck | UH | no | — |
| C | 4 | ProbeAck | C | no | — |
| C | 5 | ProbeAckData | C | yes | — |
| C | 6 | Release | C | no | ReleaseAck |
| C | 7 | ReleaseData | C | yes | ReleaseAck |
| D | 4 | Grant | C | no | GrantAck |
| D | 5 | GrantData | C | yes | GrantAck |
| D | 6 | ReleaseAck | C | no | — |
| E | — | GrantAck | C | no | — |

B and C carry the Access and Hint messages too (§9.5): a slave can forward an access to the master holding
the block, an update-based protocol.

### 1.3 Parameters (`param`)

| field | values |
|---|---|
| Arithmetic (Table 23) | MIN 0, MAX 1, MINU 2, MAXU 3, ADD 4 |
| Logical (Table 25) | XOR 0, OR 1, AND 2, SWAP 3 |
| Intent on A (Table 27) | PrefetchRead 0, PrefetchWrite 1, CBOInval 5, CBOClean 6, CBOFlush 7. On B: Prefetch only |
| Cap (Grant, Probe) | toT 0, toB 1, toN 2 |
| Grow (Acquire) | NtoB 0, NtoT 1, BtoT 2 |
| Prune (ProbeAck, Release) | TtoB 0, TtoN 1, BtoN 2 |
| Report (ProbeAck, Release) | TtoT 3, BtoB 4, NtoN 5. NtoN is illegal in ProbeAckData, Release and ReleaseData |

Permissions are None, Branch (read-only, above the tip), Trunk (on the path to the tip, no access) and Tip
(read-write, the serialization point). Only a Tip with no branches may Put or run atomics (Table 29).

### 1.4 Beats, flow control, ordering

- **Handshake.** A beat transfers when valid and ready are both HIGH. valid must not depend on the same
  cycle's ready. A low-priority valid must not depend combinationally on a high-priority valid. A
  high-priority ready must not depend on a low-priority ready (§4.1).
- **Bursts.** A message with data has max(1, 2^size / w) beats; every other message is one beat.
  - Bursts on one channel are not interleaved.
  - Control fields, including `*_address`, stay constant for the whole burst (§4.1, §8.3). Only the
    data and mask change: beat k carries the bytes at address + k·w. The model keeps the two apart as
    the wire address (constant) and the beat's effective address (derived).
  - Before its first beat is accepted, a message may be retracted or replaced. After that it is in
    progress and must finish.
  - A retracted response must still be sent eventually (§4.1).
- **Response timing (§4.2).**
  - A response's first beat may be valid in the same cycle as the request's first beat, never earlier.
  - Later response beats may lag by any amount.
  - A master must accept a concurrent response, or buffer it.
- **Errors (§4.4).**
  - `corrupt` is per beat and appears only on data messages.
  - `denied` is on D only and means the operation had no effect.
  - A denied data response must mark every beat corrupt.
  - All beats are always sent.
- **Byte lanes (§4.5).**
  - Data is little-endian and naturally aligned: address & (2^size − 1) = 0.
  - mask is LOW on inactive lanes and HIGH on all active lanes, except for PutPartialData.
  - A dataless message larger than the bus has all mask bits HIGH.
- **IDs (§6.4, Table 14).**
  - A and C route by address; B and D route by source; E routes by sink.
  - An outstanding A source is unique per link, and C requests (Release, ReleaseData) have their own
    namespace. The widths o and i therefore bound the transactions a link can have outstanding: 2^o
    requests per channel namespace, 2^i unanswered Grants.
  - On B, the (source, address) pair is unique.
  - D sinks are unique among unanswered Grants.
  - What a response copies is channel-specific: a D response to an A request copies `source` (D has no
    address); a C response to a B request copies `address` (its `source` may be any of the sender's,
    §6.4); an E GrantAck copies `sink`. ID spaces are link-local, so crossbars remap them.
- **Operation ordering (§6.5).**
  - Responses come in any order.
  - A slave responds only once the effect is visible everywhere.
  - Once it has responded to Y, any later Z is ordered after Y.
  - Fences are built by waiting for acknowledgements.
- **Forward progress (§5).**
  - Rule 1: the agent graph has no cycle.
  - Rule 2: an agent eventually presents every beat of a *received* message, that is of any message
    whose first beat the receiver has accepted (§5.1 "received"). The non-conforming example of §5.2
    is an agent that starts a burst, has its first beat accepted and then withholds the rest.
  - Rule 3: unless it has a higher-priority message in flight or unanswered, it eventually accepts
    presented beats (3i) and eventually answers requests (3ii).
  - Timeouts inside TileLink are forbidden; watchdogs that reset are allowed.
  - A NoC may be a single agent, even if it is cyclic inside.
- **TL-C concurrency limits, per block (§9.2):**
  - **Master:**
    - No Acquire while a Grant or AccessAck is pending.
    - After an Acquire, no Acquire, Get, Put or Atomic until the Grant.
    - No Release while a Grant or AccessAck is pending.
    - After a Release, nothing (including ProbeAck) until the ReleaseAck.
  - **Slave:**
    - No Grant while a ProbeAck is pending.
    - After a Grant, no Probe until the GrantAck.
    - At most one Probe outstanding per master and block.
    - No AccessAckData while a ProbeAckData is pending.
  - **One Acquire per block (interpretation).** §9.2 first forbids a second Acquire on a block until
    the Grant, then allows a write-miss-under-read-miss Acquire with a distinct source. The plan takes
    the strict reading: a master has at most one Acquire outstanding per block. It matches Octopus's
    per-line FCFS at the L1 and the directory tables' single transient per line. The monitor's M-CONC
    enforces the strict reading, with a `allow_acquire_under_acquire` switch for experiments.
  - **Probe while a Grant is pending:** the master must still answer the Probe.
  - **ProbeAckData.TtoT:** only if C is ordered per address.
  - **Merged flows (Figure 30):** a slave probing a block must also accept a voluntary Release of
    it, and it Grants only after every ProbeAck has arrived.

### 1.5 Errors in the spec (the implementation follows the intent)

| place | as printed | intended |
|---|---|---|
| Table 13, A opcode 2 | ArithmeticLogic | ArithmeticData |
| Table 27, param 7 | CCBOFlush | CBOFlush |
| Table 33 caption | "on Channel B" | AcquirePerm is on Channel A |
| Table 35 | ProbeBlock (7) | ProbePerm (7) |
| Table 38, d_data | type C, width z | type D, width 8w |
| Table 41 (Release) | no c_corrupt row | reserved, 0 (text) |
| §9.3.5/6, §9.3.10/11 | "Shrink" category | "Prune" (Table 31) |
| §9.1.4 | "six subsets", then four | four categories |
| Table 49, ArithmeticData on B | (3) | 2 |
| §9.5.7, LogicalData on B | "encoded as 2" | 3 (Table 50) |
| §9.5.8 | Intent on B references Table 27 | Table 51, Prefetch only |
| §9.5.9 | HintAck on C routed by c_source | C responses route by address (Table 14) |
| Figure 7 text | "6 messages", then 5 | 5 |
| §9.1 | "Tip (with Branches) … has read and write permissions" | Table 29: Get only; the plan follows Table 29 |
| Figure 10, third transaction | a_opcode 1 (PutPartialData) answered by d_opcode 1 (AccessAckData), which Table 12 forbids | the text calls it a one-beat atomic: ArithmeticData, size 3 (golden `fig10.txt`) |
| Figure 18 | data drawn as 0xab / 0x0 / 0xf / 0x3 while the caption says 0xabcd / 0x0000 / 0xffff / 0x00ff, and the last Get cannot return 0x3 after PutPartialData(mask 0x3, data 0xf) on a zeroed word | the golden compares the A data as drawn and the last D data as a memory returns it (0xf) |

## 2. Scope

**In:**
- All three conformance levels, cycle-level and beat-accurate on standalone links and on the Octopus
  crossbar (plan §5.3 I-A). Over the Network engine the channels are message-level (plan §5.3 I-C).
- A conformance monitor that serves as the test oracle.
- Crossbar and adapters.
- A TL-C cache master and an inclusive coherent manager.
- A standalone driver.
- An Octopus adapter: TileLink over a crossbar first, then over the Network engine.

**Out (plan §9):**
- Combinational same-cycle responses.
- Clock and power crossings.
- AXI and other legacy bridges with their timeouts.
- Multi-level TL-C hierarchies beyond one manager level.
- Update-based policies that use B-channel Put and Get. The monitor accepts these messages; no agent
  emits them.

## 3. Architecture of the component

### 3.1 Placement and layering

The component lives in `TileLink/` as its own CMake project: a library `tilelink`, an executable `tlsim` and
tests. It depends only on C++17. Octopus adds it with `add_subdirectory(TileLink)` and keeps its adapter on its
own side (plan §5). Namespace `octopus::tl`, so the library can be lifted out unchanged.

```
TileLink/
  include/tilelink/  Types.h Params.h Msg.h Channel.h Link.h Agent.h Kernel.h Monitor.h ...
  src/core/          encodings, beat math, kernel, config reader, stats, beat trace
  src/agents/        FuzzMaster TraceMaster CacheMaster RamSlave ErrorSlave CoherentManager
                     Xbar Buffer WidthWidget Fragmenter SourceShrinker Throttle
  src/check/         Monitor (per link), GraphCheck, Watchdog, CoherenceCheck, ValueCheck
  src/coh/           Policy engine: TL-C state tables (CSV) shared with Octopus
  tools/tlsim.cpp    standalone driver
  configs/           example systems
  test/              unit, golden-waveform, negative and fuzz tests
```

### 3.2 Timing model

- **One clock per kernel.** Every link and agent in a kernel shares it, as §3.2 requires per link. Inside
  Octopus the kernel ticks at the L1/LLC period; a preset whose L1s and LLC have different periods needs
  a clock-crossing adapter, which is out of scope (plan §9), and is refused at elaboration.
- **Three phases per cycle:**
  1. **offer:** each sender sets valid and its beat from its state at the start of the cycle;
  2. **accept:** each receiver sets ready from its state, and may look at the offered beat (§4.1 allows
     this);
  3. **commit:** beats with valid && ready transfer, then agents update their state.
- **Rules enforced by construction.** Because valid is fixed before ready is computed, "valid must not
  depend on ready" holds automatically, and so do the priority dependency rules.
- **No same-cycle response, an implementation choice the spec permits.** An agent sees an accepted beat
  at commit time and responds in the next cycle at the earliest. §4.2 allows, and never requires, a
  response in the request's own cycle; modeling it would need fixed-point evaluation across agents.
  The monitor still checks the spec's bound (a response is never earlier than its request's first
  beat).
- **No combinational pass-through anywhere.** Every agent, `Buffer` and `Xbar` included, adds at least
  one cycle between accepting a beat and offering it onward: an offer in cycle t is computed from state
  at the start of t, so it can only carry what was committed in t−1. A `Buffer` of depth 0 therefore
  means "no buffer instantiated, the link connects directly", not a same-cycle wire, and there is no
  `flow` mode. The ready of phase 2 may look at the offer of phase 1 on the same link (§4.1 allows it),
  but never at another link's ready; that is what keeps the evaluation a single pass with no
  fixed point.
- **Latency comes from agents and buffers:**
  - `Buffer` has a depth per channel (`pipe` behaviour: one cycle in, one cycle out).
  - `RamSlave` has a fixed or random service latency.
  - `Throttle` withholds ready randomly. That is legal per §5.2 ("waiting for a refresh") and is the
    main stress tool.
- **Reset.** `reset_cycles` defaults to 100 (the spec minimum) when standalone and to 0 inside Octopus.
  The monitor checks that valid stays LOW during reset.
- **Determinism.** Agents are evaluated in a fixed order, and all randomness comes from one seeded RNG
  per agent.

### 3.3 Messages and beats

`tl::Msg` holds one message:
- the fields channel, opcode, param, size, source, sink, address and denied;
- per-beat mask, corrupt and data;
- simulator-only fields: id, emitting agent, timestamps.

A channel moves a handle plus a beat index, so a burst is not copied per beat. The monitor sees each beat as
the wire would carry it: the constant `address` field, that beat's mask slice and its data; the beat's
effective address, address + beat × w, is derived for the transcript and the value checker and is never
a wire field.

Data has three modes, for the standalone agents:

| `data_mode` | carried | cost | used by |
|---|---|---|---|
| `none` | sizes only | lowest | timing-only studies |
| `stamp` | one 64-bit value per 8 bytes, a write counter | low | value checker on long fuzz runs |
| `full` | real bytes, with atomics computed | highest | atomics, PutPartialData and waveform tests |

The Octopus adapter (plan §5.2) has no mode of its own: it carries `Message::data` unchanged. Trace-driven
runs carry the zero lines the CPU model produces today; the external frontend (`ExternalCPU::addRequest`,
the gem5 path) carries the real bytes it is given, as it does on every other interconnect. Nothing that
carries data today loses it.

### 3.4 Links and channels

A `Link` has:
- its parameters (w, a, z, o, i) and conformance level, from which it derives its channels (A+D, or A–E);
- one `Channel` per direction, holding the offered beat, the burst lock (the in-progress message) and stats;
- an optional attached `Monitor`.

Elaboration checks each agent's declared capabilities against the link's level. For example, a TL-UL slave
on a link carrying bursts is an elaboration error.

### 3.5 Agents

| agent | role | notes |
|---|---|---|
| `FuzzMaster` | random legal traffic at a chosen level | op mix, address set, size range, source pool of 2^o, optional fences; TL-C variant caches and acquires |
| `TraceMaster` | replays an Octopus trace (`trace_C*.trc.shared`) | two modes: `uncached` issues each Load/Store as a Get/Put on a TL-UL/UH link (system 2); `cpu` feeds the Loads/Stores to a `CacheMaster` as its CPU side (system 4), which is the mode that exercises TL-C. The two exercise different behaviour and are never mixed in one test |
| `CacheMaster` | L1 with TL-C master port | states from the policy engine (plan §3.7); answers Probes while a Grant is pending, holds ProbeAck until ReleaseAck |
| `RamSlave` | TL-UL/UH memory | Get, Put, PutPartial, atomics (full data); Intent → HintAck, which is complete for a slave with no caches inward of it (a prefetch hint may be ignored, §8; a CBO has nothing to clean or invalidate); latency, queue depth, optional FIFO domain (§6.3), denied ranges |
| `ErrorSlave` | denies everything | error-path tests |
| `CoherentManager` | inclusive TL-C slave on the inner side, TL-UH master on the outer side | directory of branches and tip per block; blocks a block from Grant to GrantAck; merges a voluntary Release with an ongoing probe (Figure 30); serves an uncached Get by probing a writable owner toB and an uncached Put or Atomic by probing every holder toN before executing and acknowledging it (§6.5; plan §8 decision 8); evicts a block of its own volition (§9.3.3) by ProbeBlock toN to every holder, then PutFullData outward once every ProbeAck[Data] is in; tracks "data not valid" after an AcquirePerm (§9.3.2) until a ReleaseData, a ProbeAckData or a full-block PutFullData arrives, and serves a Get on such a block by probing the holder first; **cache management operations** (§8.2.3), received as Intent on its inner A: CBOInval = ProbeBlock toN to every holder, every ProbeAck[Data] collected, its own copy dropped, dirty data discarded; CBOClean = ProbeBlock toB to every holder, dirty data (from ProbeAckData or its own copy) written outward with PutFullData and the AccessAck awaited, copies kept at Branch; CBOFlush = Clean then drop. HintAck is sent only when the outer AccessAck is in and the inner permissions are reduced, so it carries completion (§8.2.3). A CMO is a slave-side transaction on the block, serialized with Acquires exactly like a Grant→GrantAck window: no Grant or Probe on the block from the Intent to the HintAck. An outer link that denies the write-back makes the HintAck denied (§8.2.3) |
| `Xbar` | N masters × M slaves | A/C decode by address map, B/D by source range, E by sink range; remaps source and sink into one namespace per side (§6.4); per-output arbitration (round-robin, fixed, TDM) holding the grant for a whole burst; the C, D and E paths accept beats independently of A and B occupancy (a high-priority ready never depends on a low-priority ready, §4.1), which is what makes the crossbar a conforming agent |
| `Buffer` | per-channel queues | depth per channel; one cycle in, one cycle out (plan §3.2) |
| `WidthWidget` | w conversion | every opcode and level: it repacks beats only. Narrow→wide merges 2^k beats into one (mask concatenated, corrupt OR-ed); wide→narrow splits one beat into 2^k (mask sliced, corrupt copied to every slice). Control fields untouched; a single-beat dataless message stays one beat with its mask re-derived (§4.5). Source and sink pass through, so it is legal on TL-C links |
| `Fragmenter` | splits an operation larger than `max_size` of the slave into 2^k operations of `max_size` | Get, PutFullData and PutPartialData only; an Atomic, an Intent or any TL-C message larger than `max_size` is an elaboration error (a split atomic would lose atomicity and carry propagation). Fragments get the same source plus a fragment index in the high source bits (so the downstream link needs o + k bits); responses are reassembled in address order into one response: `denied` is the OR of the fragments' `denied` and `corrupt` is per beat as returned. Known semantic gap, recorded as such: a fragment denied after earlier fragments were written leaves a partial effect, which the spec's "no effect" rule for a denied message cannot express; the same gap exists in rocket-chip's TLFragmenter. The ID of an operation is held until its last fragment's response |
| `SourceShrinker` | fewer source IDs downstream | TL-UL/UH links only: on a TL-C link the master's source is what routes B-channel Probes to its cache after the Acquire has completed, so a remapped source would break them; elaboration refuses it on a TL-C link. Blocks the A channel (ready LOW, legal under rule 3 because the pool drains by the slave's own rule 3ii) when the downstream pool is empty |
| `Throttle` | random ready/valid withholding at a link | legal only on the first beat of a not-yet-presented message (plan §3.6 M-BURST) |

### 3.6 Checkers

Every violation reports the rule ID, the spec section, the link, the cycle and the message. `fatal` exits with
status 2, as Octopus's `net::fatal` does. A `report` mode counts violations instead.

**Monitor rules (per link):**

| group | checks |
|---|---|
| M-LEVEL | opcode legal on that channel and at the link's level; bursts only from TL-UH up (TL-UL: 2^size ≤ w) |
| M-PARAM | param range per opcode (plan §1.3); reserved params are 0; Grant and GrantData never carry toN, denied or not, and a successful Grant's cap meets or exceeds the Acquire's request (plan §8 decision 7) |
| M-ALIGN | address aligned to size; size within 2^(2^z − 1) and the slave's declared maximum; address fits in a bits |
| M-MASK | the §4.5 rules, including PutPartial within its region and Get/Acquire contiguous |
| M-RSV | reserved fields: corrupt on dataless messages, denied on ReleaseAck |
| M-BURST | beat count; control fields constant; no interleaving; an in-progress message is not abandoned; a retracted D or C response is eventually re-offered |
| M-RESP | the response opcode is allowed for its request (Table 12); size copied; the response is matched to its request per channel (plan §1.4): D by `source` (and `opcode` namespace, A vs C requests), C by `address`, E by `sink`; first response beat not before the request's first beat |
| M-ERR | denied only on D; denied data responses corrupt on every beat; a denied Grant changes no permission |
| M-ID | the uniqueness rules of plan §1.4 |
| M-CONC | the §9.2 limits on both master and slave sides, per block |
| M-PERM | the monitor tracks each master's permission per block on the link: Acquire param matches it (NtoB from N, BtoT from B); a ProbeAck/Release param starts from it; the Grant cap is at least the requested permission. A denied Grant is exempt: its `param` is ignored and the tracked permission stays unchanged (§9.3.7) |
| M-RESET | valid LOW during reset |

**System-level checkers:**
- **GraphCheck.** At elaboration it builds the agent graph from links and fails on a cycle, printing the
  path (rule 1). An agent that decouples its links (the DMA example in §5.1) declares them as separate
  agents.
- **Watchdog, per message.** Every presented beat and every received request carries its own timer. A
  beat presented and not accepted, a burst in progress with no further beat accepted, or a request
  received and not answered for longer than `progress_bound` cycles trips it; the dump names the link,
  the message and the rule it points at (3i, 2, 3ii). `progress_bound` is a declared bound of the
  configuration (the sum of the slowest slave's latency, the deepest queue and the largest burst, times
  the number of masters is the default formula), because without one a finite timer cannot tell an
  illegal permanent stall from a legal long delay (§4.3). A trip is therefore a *diagnostic*, with the
  bound printed, not a conformance verdict; the spec allows watchdogs and this one never recovers. A
  global "no beat anywhere" timer would miss a single starved request behind busy links, so there is
  none.
- **What the checkers cannot show, said plainly.** The monitor observes beats; it cannot see
  combinational dependencies between valid and ready. Those rules hold in the standalone simulator by
  construction of the cycle (plan §3.2), and that is the argument, not an observation. Likewise "0
  monitor violations" means no observed violation of the rules the monitor implements, not
  conformance; conformance rests on three legs: the monitor (protocol rules), the watchdog with a
  declared bound (progress), and the elaboration-time arguments (graph acyclicity, the admission
  contract of plan §5.2, the resource argument of plan §5.3).
- **CoherenceCheck (TL-C).** It checks single-writer/multiple-reader per block:
  - At most one master holds T, and then no other master holds B.
  - The check runs on the manager's view when it issues a Grant (all ProbeAcks in) and on the masters'
    view when they accept a Grant or send a ProbeAck or Release.
- **ValueCheck** (`stamp` or `full`). Acknowledgement order is not write order: §6.5 lets the slave
  serialize two outstanding writes either way, so with W1 = 1 then W2 = 2 both outstanding and W2
  acknowledged first, a later read may legally return 1 or 2. The checker therefore keeps, per byte, a
  *set* of admissible values, as rocket-chip's TLRAMModel does:
  - a write adds its byte value to the set when it is issued (masked bytes only);
  - when a write is acknowledged and no other write to that byte was outstanding during its whole
    issue-to-acknowledgement interval, the set collapses to that write's value;
  - otherwise the set keeps every value of the writes whose intervals overlapped, until a later write
    collapses it;
  - a read's returned byte must be in the set as it stood at the read's issue, plus the values of
    writes outstanding during the read's interval;
  - an atomic's returned old value is checked the same way, and its written value is computed from
    the returned old value, so a wrong old value is caught once, not twice;
  - a denied or corrupt beat is not checked, and a denied write adds nothing.
  With TL-C on, this runs end to end: L1 hits read the cached copy and count as reads issued at the
  hit. Under `stamp` the counter is the byte value; under `full` the bytes are.

### 3.7 The TL-C policy engine (shared with Octopus)

The coherence policy is a pair of state tables (L1 master and inclusive manager) in the Octopus FSM CSV format
(events and states by row position, as `FSMReader` reads them). Both simulators run the same tables.
- **The library** (`src/coh/`) has its own reader of that format, so T1–T6 stay free of any Octopus
  dependency; it decodes a TileLink message and the line state into an event, and an action into the
  TileLink message to send.
- **The standalone agents** execute those actions directly.
- **Octopus's protocol handlers** wrap the same calls and turn actions into `ControllerAction`s (plan §5.3).

Writing the tables once is what keeps the two simulators from drifting apart.

## 4. The standalone simulator (`tlsim`)

- **Configuration** uses Octopus's CSV parameter format (`name(type),value`, per-instance `name[k]`). The
  library has its own small reader, so the Octopus adapter can pass its `ParametersMap` entries through.
- **The system file** lists agents (`agent[k].type(s)`, the agent's own parameters) and links
  (`link[k].master(s)`, `.slave(s)`, `.w(i)`, `.o(i)`, `.level(s)`, ...).
- **Example systems in `configs/`:**
  1. one FuzzMaster and one RamSlave, TL-UL;
  2. 4 FuzzMasters → Xbar → 2 RamSlaves, TL-UH, mixed w with WidthWidget and Fragmenter;
  3. 4 CacheMasters → Xbar → CoherentManager → RamSlave, TL-C;
  4. system 3 driven by TraceMaster over EEMBC traces.
- **Outputs:**
  - `TLStats.csv`: per channel, beats, busy cycles and stall cycles (valid && !ready); per opcode, latency
    from the request's first beat to the response's last beat, and the outstanding-ID histogram;
  - the beat transcript (`trace=1`), one line per accepted beat:
    `cycle link ch opcode param size source sink address beat mask data corrupt denied`, the golden format
    for tests;
  - the checker report.

## 5. Integration into Octopus

### 5.1 Octopus's directory protocols cannot run on TileLink unchanged

The directory MSI/MESI/MOESI FSMs follow the three-hop textbook protocol:
- an owner sends data straight to the requester (`Data2Req`, `Data2Both`);
- sharers send InvAck to the requester, which counts them (`AckCount`, `Data_nonZeroAck`).

TileLink has no master-to-master message. Every Probe answer goes to the slave, and the requester gets one
Grant after the slave has collected all ProbeAcks: four hops, plus GrantAck. TileLink therefore needs its own
TL-C tables. The correspondence, which keeps the event trace and the reports readable:

| Octopus directory | TL-C | channel | kind reused |
|---|---|---|---|
| GetS | AcquireBlock NtoB | A | `K_GETS` |
| GetM from I | AcquireBlock NtoT | A | `K_GETM` |
| GetM from S (upgrade) | AcquireBlock BtoT (the reply is Grant or GrantData, §9.2: data if probed meanwhile) | A | `K_GETM` |
| (none: Octopus traces are word Loads and Stores, never a full-line write) | AcquirePerm NtoT is used by the standalone `FuzzMaster` only | A | — |
| Fwd-GetS | ProbeBlock toB (owner answers ProbeAckData TtoB **to the manager**) | B | `K_FWD` |
| Fwd-GetM | ProbeBlock toN | B | `K_FWD` |
| Inv, InvM (a sharer or owner invalidated for a writer, or for the LLC's own eviction) | ProbeBlock toN; the manager's eviction then writes back outward after the last ProbeAck[Data] | B | `K_INV` |
| InvAck | ProbeAck BtoN / NtoN | C | `K_ACK` |
| owner's data on a forward | ProbeAckData TtoB / TtoN | C | `K_WB_DATA` |
| PutS / PutE | Release BtoN / TtoN | C | `K_PUTS` / `K_PUTE` |
| PutM | ReleaseData TtoN | C | `K_PUTM` |
| PutAck | ReleaseAck | D | `K_ACK` |
| Data from home (incl. exclusive) | GrantData toB / toT | D | `K_RESP` |
| (none) | GrantAck | E | new `K_GRANT_ACK` |
| AckCount | (none; the manager counts) | — | — |
| memory read / write | Get / PutFullData on the outer TL-UH link | A | `K_MEM_READ` / `K_MEM_WRITE` |
| fill | AccessAckData | D | `K_FILL` |
| `K_MEM_WACK` (plan M1) | AccessAck | D | `K_MEM_WACK` |

Consequences:
- **MESI maps directly.** The manager may grant toT on a read when no other master holds a copy (§9.3.7:
  a Grant may exceed the request); the L1 keeps clean versus dirty as policy state on top of T.
- **MOESI's O does not exist among sibling L1s.** A Branch is read-only, so a dirty owner that is
  downgraded writes back (ProbeAckData TtoB) and the inclusive manager becomes the dirty Tip. MOESI on
  TileLink is therefore a manager-side policy, not an L1 state. The TL-C presets are MSI and MESI.
- **No Octopus FSM is edited.** The TL-C tables are new files: `TLC_MESI_L1.csv`, `TLC_MESI_LLC.csv`, MSI
  as a subset.
- **M1's write acknowledgement becomes standard.** TileLink's AccessAck to a PutFullData is the write
  acknowledgement M1 added (`K_MEM_WACK`), so H1 (the LLC holds a write until it is acknowledged) carries
  over unchanged.

### 5.2 Carrying TileLink in Octopus messages

- **Message fields.** `Message` gains a small TileLink header: channel (0 = not TileLink), opcode, param,
  size, source, sink and denied. `copy()` copies it, and no other code reads it.
- **Kinds.** `kind` is set from the table in plan §5.1, so `OCTOPUS_TRACE`, the LatencyReport stages and
  octoviz work unchanged; there is one new kind, `K_GRANT_ACK`.
- **The TileLink interface is a `CommunicationInterface` (`TLPort`):**
  - one receive queue per channel, served highest priority first (E, D, C, B, A), so a stalled B (a Probe
    held until ReleaseAck) never blocks the D that unblocks it;
  - ready on a channel means "that receive queue is not full"; a beat is accepted, in the spec's sense,
    when it enters the queue. The controller's later pop is processing, not acceptance; the monitor
    times its rules (M-RESP, M-CONC) at the enqueue;
  - `pushMessage` reads the channel from the TileLink header, not from `MessageType`;
  - **transmit-side admission, the contract.** Today `CacheController::txNeed` hard-codes the directory
    protocols' needs by message kind and three queue categories (`req`/`fwd`/`resp`, plus `up_fwd`/
    `up_resp`); it does not derive them from `consumedBy`/`emittedBy`, and `txFreeSlots`/`txReserve`/
    `txRelease` are keyed by `MessageType`. For TileLink the need is derived from the table instead:
    1. **Prediction.** Before a message is popped, the handler looks up the cell (line state, event)
       the message would take and sums, per channel, the messages its actions emit (`emittedBy` with a
       channel field in `MsgClass`); an action whose fan-out depends on the directory (a Probe to every
       holder) counts the current holder count, which cannot grow while the message waits because the
       line is blocked. The self-generated Replacement event is predicted the same way from its own
       cell. Saved-request completions (a parked GetS answered later) keep the reservation they took
       when parked, as today (`m_tx_holds`, `parked`).
    2. **Resources checked.** Queue slots per channel on the right port (inner or outer), and the ID
       pools: an emission on A or C needs a free source in its namespace, a Grant needs a free sink.
       All are reserved together or the message is not popped; a message that needs nothing is popped
       unconditionally (that is what makes E, D and C sink unconditionally at their consumers).
    3. **Release.** The reservation is released when the transition and its parked data-array actions
       have run, per channel, as `txRelease` does today; the source or sink is released when its
       response is accepted, by `TLPort`, not by the controller.
    4. **Check.** `checkProtocols` (plan §5.3) also verifies, cell by cell, that every emitted channel is
       one the prediction counts, so a table cannot emit on a channel it did not reserve.
    The three-queue interconnects keep `txNeed` as it is; they never see TileLink tables;
  - `TLPort` allocates sources from a 2^o pool per namespace (A, and C for Release/ReleaseData) and
    restores them on the response; it keeps a sink per unanswered Grant from a 2^i pool.

### 5.3 Steps of integration

1. **I-A, TileLink over a crossbar** (new `interconnect_type TileLink`):
   - L1s and LLC banks attach to a library `Xbar` driven from Octopus's clock (a `ClockedObj` that ticks
     the kernel at the L1/LLC period, plan §3.2);
   - protocol types `TLC_L1` and `TLC_LLC` wrap the policy engine and declare `consumedBy`/`emittedBy`
     with channels;
   - memory stays on the point-to-point memory bus with H1/H2;
   - **accounting:** the crossbar logs every accepted message as a Logger bus event (A/B as `REQ_BUS`,
     C/D/E as `RESP_BUS`, the outer link as `MEM_BUS` once I-B exists), so the LatencyReport bus stages
     and the event-path tiling stay valid, as M4 did for memory routes; octoviz learns the new kind
     `K_GRANT_ACK` and the TileLink resource. Without this TL-OCT-A's tiling criterion cannot pass.
   - **what the stages mean under TileLink.** `Logger::finalizeEvents` picks milestones (first CPU
     ENTER, first REQ_BUS EXIT, first LLC ENTER/SERVICE, last LLC EXIT, last RESP_BUS EXIT, last CPU
     EXIT). TileLink adds a probe round trip inside the manager's service, owner data that returns via
     the manager, and a GrantAck after the data. The definitions, so that the columns keep their
     meaning:

     | column | TileLink events |
     |---|---|
     | CPU latency (Total) | unchanged: CPU issue → data accepted by the L1 (GrantData or AccessAckData on D). The GrantAck is outside it |
     | Req-Bus | the A message, L1 accept → manager accept |
     | LLC (service) | manager accept → Grant offered; it contains the probe round trip (B out, C back) and, on a miss, the outer Get and its AccessAckData, which the Mem-Bus and DRAM stages carve out as today |
     | Resp-Bus | the D message, manager → L1. Owner data never travels L1 → L1; the C leg (owner → manager) is inside LLC service, and a new `Probe` column (first B EXIT → last C EXIT) reports it |
     | Completion (new column) | Grant accepted → GrantAck accepted at the manager: the time the block stays blocked after the requester already has its data |

     Event-path tiling then checks that Req-Bus + LLC + Resp-Bus + the L1 stages sum to Total, as now;
     the Probe column is informational, inside LLC. The tiling proves the stages partition the
     latency; the table above is what gives the labels their meaning.
2. **I-B, memory over TileLink.**
   - The LLC's upper interface becomes a TL-UH master link: Get answered by AccessAckData, PutFullData by
     AccessAck.
   - A `TLMemSlave` adapter wraps `MainMemoryController` or `MCsimInterface`. Admission works as today
     (complete only with a response slot free).
   - The hazard detector's hooks (`OCTOPUS_MEM_HAZARD`, today in `BusInterface`, `MainMemoryController`,
     `MCsimInterface`) move into `TLMemSlave` and the LLC's outer `TLPort`, so the 0/0 criterion still
     means something.
3. **I-C, TileLink over the Network engine.**
   - **Message-level, not beat-level.** The TileLink link ends at the network interface; inside, the
     Network carries flits (`flit_bytes`, `header_bytes`), which are not TileLink beats (w). On this path
     a channel carries whole messages (w = line size, every message one beat) and the monitor checks the
     message rules (opcode, param, IDs, responses, concurrency), not the burst rules. Beat accuracy is
     I-A's.
   - **Classes become channels.** The NoC is a TileLink agent (§5.3 allows a cyclic mesh as one agent)
     whose boundary is the network interfaces.
   - **Without memory on the network: 5 classes**, A < B < C < D < E.
   - **With memory on the network: 7 classes.** With the NoC as a single agent the agent graph has the
     cycle NoC → LLC → NoC whatever the class count (the LLC is a slave of the NoC on the inner link and
     its master on the outer link). Giving the inner and outer links disjoint classes, i.e. disjoint
     buffers, is what lets the NoC be modelled as two decoupled agents, inner and outer, in the sense of
     §5.1's DMA example; the graph L1 → NoC_in → LLC → NoC_out → memory is then acyclic.
   - **The 7-class order is A_in < B < C < A_out < D_out < D_in < E**, chosen so that every
     consume-then-emit edge goes up:

     | consumer | consumes | emits |
     |---|---|---|
     | LLC | A_in | B, D_in, A_out |
     | LLC | C | D_in, A_out (write-back) |
     | memory | A_out | D_out |
     | LLC | D_out | D_in |
     | L1 | B | C |
     | L1 | D_in | E |

     **The one edge that goes down, and the resource argument that covers it.** The LLC *does* evict on
     a fill: `CacheDataHandler_COTS::updateLineData` picks a victim when the fill finds no free way
     (`moveLine2WB`), and `CacheController::checkReplacements` then raises a self-generated
     Replacement (`K_EVICT`) whose cell emits the write-back on A_out, below D_out. `checkProtocols`
     cannot see it: it orders a cell's emissions against the class the cell *consumes*, and a
     Replacement consumes nothing from the network (`cin < 0`). So the class order alone does not
     prove D_out drains; the following does, and it is the same argument that holds today for
     `K_FILL` and `K_MEM_WRITE` sharing RESPONSE:
     1. a fill is always sunk: `updateLineData` writes it into the MSHR entry its request allocated or
        into the write-back register, and neither path can refuse (the MSHR entry exists because the
        read was issued; the PWB map is unbounded in code, bounded in practice by step 2);
     2. the eviction's write-back waits only for A_out slots and, through `pwbHasSpace`, blocks only
        *new demand* admission while it waits, never a fill or any C/D/E message;
     3. A_out drains because memory consumes it unconditionally (admission at the memory controller
        completes a request only with a D_out slot free, plan M2) and D_out drains by step 1.
     Two things make this an argument rather than a hope, and both are built: the admission contract
     of plan §5.2 reserves A_out for the Replacement cell before it is popped (step 2 is then exact,
     not approximate), and TL-OCT-EVICT (plan §6) forces evictions on fills under tiny queues and
     checks that no fill is ever refused. `checkProtocols` additionally lists every self-consumed cell
     that emits, so the exception is visible in the verdict instead of silent.
   - **What `checkProtocols` verifies, and what it does not.** Octopus's rule "emit only a higher class"
     is a sufficient condition for every virtual network to drain; it is *stronger* than TileLink's.
     TileLink's own per-agent priority for the LLC (§5.1) is A_in < A_out < B < C < D_out < D_in < E, so
     the spec lets the LLC write back on A_out after consuming C, a lower-priority emit, and relies on
     rule 3 instead. The 7-class order above moves A_out above C to satisfy Octopus's rule; that is
     legitimate for deadlock freedom of the simulator, but it means the Network enforces **its** rule,
     not TileLink's priority and flow-control rules. Those are checked only by the monitors at the
     endpoints.
   - **Cost.** Each class owns `vcs_per_class × vc_depth` flit buffers per router port, so 7 classes
     carry about 2.3× the buffering of today's 3. The alternative, 5 classes with the LLC's outer link on
     a separate endpoint pair of its own, is decision 6 in plan §8.
   - **`Classifier`** gains `num_classes` 5 and 7, keyed on the TileLink channel and on whether the
     endpoint is a memory port.
   - **No point-to-point ordering is needed** (`requires_ordering none`): TileLink's GrantAck and the
     hold-ProbeAck-until-ReleaseAck rule exist for exactly that, the tables implement the hold, and they
     never emit ProbeAckData TtoT. The `K_ACK`-in-FORWARD special case of today's `Classifier` has no
     TileLink counterpart.

Unchanged: CPU, cache data arrays and replacement, the LLC data handler, the existing FSM tables and
protocol handlers, the bus interconnects. The Network engine changes in four places, not one:
`Classifier` (5/7 classes keyed on the channel), `NetConfig::classPriority` and the `num_classes`
validation (`Network.cpp`), `NetworkInterface::txClass`/`classOfType` (today a three-`MessageType` map;
becomes channel-keyed through the admission contract), and `ProtocolCheck` (class names, `classOf`
building messages with the TileLink header, the self-consumed-cell listing). Routers and VC storage are
already parameterized by class and need no change. Logger and LatencyReport gain the two columns of
plan §5.3 I-A and the TileLink bus events.

## 6. Validation

| id | what | pass |
|---|---|---|
| TL-UNIT | encodings, beat math, mask derivation, address map decode, source remap | unit tests |
| TL-WAVE | spec Figures 6, 7, 8–10, 11, 12, 17, 18, 22 rebuilt as directed scenarios | beat transcript equals the figure, beat for beat (only the cycle offset differs where the figure shows a same-cycle response) |
| TL-NEG | one mutant agent per monitor rule (wrong param, interleaved burst, reused source, Grant before ProbeAck, ProbeAck before ReleaseAck, ...) | each run fails with exactly that rule ID |
| TL-GRAPH | a cyclic agent graph; the coupled DMA example of §5.1 | refused at elaboration, path printed |
| TL-PROGRESS | the §5.2 examples: periodic forwarder (non-conforming), withholding beats (non-conforming), slow arbiter and refresh-waiting DDR (conforming) | watchdog names rule 3ii, then rule 2, for the first two; the conforming ones complete |
| TL-FUZZ-U | TL-UL/UH fuzz: w ∈ {4, 8, 32}, Xbar 1–8 × 1–4, Fragmenter, WidthWidget, Throttle, `full` data, 10 seeds | 0 monitor violations, ValueCheck clean, all requests answered |
| TL-FUZZ-C | TL-C fuzz: 2–8 CacheMasters, small address set (high contention), Throttle everywhere | 0 violations, CoherenceCheck and ValueCheck clean, no watchdog |
| TL-FLOWS | Figures 29 and 30 as directed races | the documented serialization and the right Grant type (data after a probe) |
| TL-UNCACHED | an uncached `FuzzMaster` (Get, Put, Atomic) beside 2–4 `CacheMasters` on one manager, `full` data, high contention; the directed case of plan §8 decision 8 (a Branch holder, then an uncached Put, then the holder reads) | ValueCheck clean: no holder ever reads a value older than an acknowledged Put; every Put/Atomic preceded by ProbeBlock toN to every holder, every Get by at most a toB probe of the owner (transcript check); CoherenceCheck clean |
| TL-CMO | CBOInval, CBOClean, CBOFlush against a manager with 1–3 holders, clean and dirty, racing an Acquire and a voluntary Release on the block | HintAck only after the outer AccessAck and the inner ProbeAcks; permissions afterwards exactly N / ≤B / N; dirty data in memory after Clean and Flush, absent after Inval; the racing Acquire served after the HintAck; a denied outer write gives a denied HintAck |
| TL-PERM | AcquirePerm NtoT, then (a) ReleaseData, (b) ProbeAckData on a ProbeBlock, (c) full-block PutFullData, (d) a Get from another master while data-not-valid | the manager's data-not-valid flag clears in a–c; in d the Get is served from the holder's ProbeAckData, never from the stale copy |
| TL-DENY | `ErrorSlave` behind the manager and behind a crossbar: denied Get, Put, Atomic, Grant, GrantData, HintAck | every denied data response corrupt on every beat; a denied Grant leaves the tracked permission unchanged (M-PERM exemption); the master's state unchanged; the GrantAck still sent |
| TL-ADAPT-ERR | `Fragmenter` with one fragment denied; `WidthWidget` with one corrupt input beat | the reassembled response is denied, corrupt on the fragments' beats; the widened beat is corrupt; the narrowed beats are all corrupt |
| TL-ID | masters with o = 1, 2 and the manager with i = 1 at full load; a mutant that reuses an outstanding source and one that reuses a sink | the pools block the sender (ready LOW, no violation, no watchdog trip at the declared bound); the mutants fail M-ID |
| TL-OCT-EVICT | Octopus TileLink crossbar and network, LLC sized so that most fills evict, inject/eject queues of 1–2, `pwb_size` 1 | no fill refused (counter in `updateLineData`), no deadlock, every write-back issued, `checkProtocols` lists the Replacement cell as the one self-consumed emitter |
| TL-NET-7 | Network with 7 classes, a generator per class on the standalone harness and the Octopus memory preset | every class carries traffic and drains; reservations per class balance to zero at the end (NetworkStats); the class-order negative test refused |
| TL-DET | the same configuration run 3 times | identical md5 of stats and transcript |
| TL-OCT-A | Octopus, TileLink crossbar, TLC MESI, EEMBC 10 benchmarks | 4/4 cores finish, 0 faults, 0 monitor violations, event-path tiling 0 failures (needs the I-A accounting); hazard detector 0/0 once I-B's hooks exist |
| TL-OCT-CMP | same traces: TLC MESI vs directory MESI on the bus preset | per benchmark, L1 miss and DRAM access counts within 2% (the ±2.1% precedent of plan M1; they are not equal because the cores' interleaving, hence LRU, moves with latency), and the per-core instruction counts identical; the difference in mean request latency explained by the fourth hop (owner data goes to the manager, then to the requester) and, for contended lines, by the Completion column (the GrantAck round trip) |
| TL-OCT-NET | TileLink over the Network: Grid, Grid2B, Grid2BM, Grid2B2M, FCFS and RR, tiny inject/eject queues | as TL-OCT-A; `checkProtocols` passes with the Replacement cell listed; TL-NET-7 and TL-OCT-EVICT on the memory presets; a negative test (class order broken) refused |
| TL-REG | `run.sh reg` entries for TL-OCT-A and TL-OCT-NET | pass |

The monitor stays attached to every link in every Octopus TileLink run (`tl_monitor(i)`, default 1); a
TileLink run is not valid unless it reports 0 violations.

## 7. Steps

Each step: implement, run its tests, write a section in a `docs/TileLink_Implementation_Report.md`, commit.
ASan once a sanitizer build exists on this machine (none today).

| step | work | validation |
|---|---|---|
| **T0** | this plan; spec digest (plan §1) | review before code |
| **T1** | `TileLink/` CMake project; types, encodings, parameters, `Msg`, `Channel`, `Link`, kernel and three-phase cycle; config reader; beat transcript | TL-UNIT |
| **T2** | Monitor for TL-UL/UH; `RamSlave`, `ErrorSlave`, `FuzzMaster` (TL-UL); `tlsim` with system 1 | TL-WAVE (TL-UL figures), TL-NEG (UL/UH rules), TL-DET |
| **T3** | TL-UH: bursts, atomics, Intent; `full`/`stamp` data and ValueCheck (the admissible-set rule); `Xbar`, `Buffer`, `WidthWidget`, `Fragmenter`, `SourceShrinker`, `Throttle` with the restrictions of plan §3.5; system 2 | TL-WAVE (all), TL-FUZZ-U, TL-DENY (UL/UH part), TL-ADAPT-ERR, TL-ID |
| **T4** | GraphCheck, Watchdog | TL-GRAPH, TL-PROGRESS |
| **T5** | exact TL-C tables, `TLC_MESI_L1.csv` and `TLC_MESI_LLC.csv`, written out with every cell justified | review of the tables before code |
| **T6** | TL-C: monitor M-CONC and M-PERM; policy engine; `CacheMaster`, `CoherentManager` with CMOs and data-not-valid; CoherenceCheck; systems 3 and 4 | TL-NEG (TL-C rules), TL-FLOWS, TL-FUZZ-C, TL-UNCACHED, TL-CMO, TL-PERM, TL-DENY (TL-C part) |
| **T7** | Octopus I-A: `Message` TileLink header, `K_GRANT_ACK`, `TLPort`, the admission contract of plan §5.2 (table-derived `txNeed`, ID pools), `interconnect_type TileLink`, `TLC_L1`/`TLC_LLC` handlers, Logger and octoviz accounting with the Probe and Completion columns, preset `MultiCoreSystem_TileLink` | TL-OCT-A (without the hazard criterion), TL-OCT-CMP, TL-OCT-EVICT (crossbar); every existing preset byte-identical |
| **T8** | Octopus I-B: memory over TileLink, `TLMemSlave` for both memory models, hazard hooks moved | TL-OCT-A with MCsim; hazard 0/0 |
| **T9** | Octopus I-C: the four Network changes of plan §5.3 (`Classifier` 5/7 classes, `classPriority` and validation, `NetworkInterface` channel-keyed admission, `ProtocolCheck` with the TileLink header and the self-consumed-cell listing), network presets | TL-OCT-NET, TL-NET-7, TL-OCT-EVICT (network) |
| **T10** | docs: SUPPORTED_CONFIGURATIONS, an architecture note, manual chapter; `run.sh` TileLink step and regression entries | TL-REG |

T1–T6 produce a complete standalone simulator with no Octopus dependency; T7–T9 are the integration.

Effort, at the pace of the NoC phases (one step with its tests and report per 2–4 working days, the
exact-table step longer):

| steps | weeks |
|---|---|
| T1–T4, standalone TL-UL/UH with checkers | 2–3 |
| T5–T6, TL-C tables and agents | 3 (the tables are the risk: every cell justified and reviewed) |
| T7–T9, Octopus integration | 3–4 |
| T10, docs and regression | 1 |

About 10 weeks in all; the standalone simulator is usable after T4 and complete after T6.

## 8. Decisions

Decided 2026-10-21, after review:

1. **Location.** The library is in `TileLink/` with its own CMake project, and Octopus includes it.
   In-tree now, splittable later, since it has no Octopus dependency; a separate repository used as a
   submodule stays possible if the component is published on its own.
2. **No same-cycle responses** (plan §3.2). The minimum response latency is one cycle; a combinational
   mode only if a study ever needs zero-latency slaves.
3. **TileLink header in `Message`** (plan §5.2), not fields packed into `complementary_value`/`2`. It is
   explicit, and the decoders never read it.
4. **Policies: MSI and MESI.** MOESI has no sibling-L1 counterpart (plan §5.1); to be revisited later.
5. **Branch.** A new branch `tilelink` off `network`, since T1–T6 touch nothing in Octopus.
6. **Classes with memory on the network: 7**, with `num_classes` a knob. Seven classes keep today's
   topology (bank and memory port on one router) at 2.3× the VC buffering. The alternative, 5 classes
   with the LLC's outer link on its own pair of endpoints whose traffic never shares a buffer with the
   inner link, is the same decoupling expressed in the topology instead of the classes, and remains
   open as a configuration if the buffer cost ever matters.
7. **Grant toN: never encoded** (settled 2026-10-21). The spec does not forbid it: §9.3.7–9.3.8 say only
   that the granted permission "may exceed" the request, and Tables 38–39 list the whole Cap category.
   The plan adopts rocket-chip's interoperability rule, whose monitor rejects toN on Grant and
   GrantData unconditionally, denied or not. So, as rules of the model and the monitor (M-PARAM,
   M-PERM):
   - a successful Grant meets or exceeds the request: NtoB → toB or toT; NtoT → toT; BtoT → toT;
   - a denied Grant leaves permissions unchanged and its `param` has no semantic meaning (§9.3.7), but
     it still may not encode toN;
   - this is stricter than the spec's wording, recorded as the chosen interoperability rule, not as a
     reading of the spec.
8. **Uncached accesses at the manager: invalidate on write** (settled 2026-10-21). A first reading,
   "cap every cached copy at Branch or None before a Put", was wrong: a holder left at Branch keeps a
   stale copy it may still read after the AccessAck, which §6.5 forbids ("all current cached copies of
   the data are up-to-date before the acknowledgement is sent": either probe them or forward the Put
   to them). Forwarding the Put is the update-based policy the plan excludes, so the manager's
   invalidation-based policy is:

   | uncached operation on the inner A | the manager, toward other cached holders |
   |---|---|
   | Get | ProbeBlock toB the writable owner if there is one (its ProbeAckData TtoB supplies the current data); Branch holders stay |
   | Put, Atomic | ProbeBlock toN every holder, collect every ProbeAck[Data] (dirty data first), execute, then AccessAck[Data] |

   This matches rocket-chip's broadcast manager (writes and atomics probe toN, reads toB). §9.4's
   "implicitly cap" sentence is read as describing the *requester's* own implied permission after an
   uncached access (None after a Get, at most Branch after a Put), not a probe algorithm; its wording
   is the subject of an open issue on the specification (chipsalliance/tilelink #5) and is not relied
   on.

## 9. Out of scope

- Clock or power crossings and different clocks per link (adapters per §3.2, later).
- AXI4 and legacy bridges with the timeouts of §4.3.
- Multi-level TL-C (L1 → L2 → L3, each a manager).
- Update-based policies using Access and Hint messages on B and C.
- Real-time arbitration: TDM in `Xbar` is included, while TileLink-aware TDM_XY or priority VCs on the
  Network join the deferred real-time work.
- Snooping over TileLink: TileLink is directory-style by construction, and broadcast is a manager policy
  that probes all masters.
