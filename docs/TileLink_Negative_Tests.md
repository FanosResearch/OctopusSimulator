# TileLink negative tests: TL-NEG mutants and TL-PROGRESS scenarios

Status: T2/T4/T6 design input, written 2026-10-21. One mutant per rule id of
`docs/TileLink_Monitor_Rules.md` (plan §6 TL-NEG: "each run fails with exactly that rule ID"), grouped
by level: T2 runs the TL-UL and TL-UH mutants, T6 the TL-C mutants. Then the TL-PROGRESS scenarios of
plan §6 (the four §5.2 examples) with the watchdog verdict and the `progress_bound` each needs (T4).

## 1. Harness

### 1.1 Link and agents

| item | TL-UL/UH mutants | TL-C mutants |
|---|---|---|
| link `L0` | `w=8 a=32 z=3 o=2 i=1`, `level` UL or UH as the row says, slave `max_size=6`, address map: one region [0x0, 0x10000) | `w=64 a=32 z=3 o=2 i=2 level=C`, `max_size=6`, `block_bytes=64`, `c_ordered=1` (w = 64 so that every size-6 Acquire, GrantData, ProbeAckData and ReleaseData is one beat and the fire cycles below need no burst arithmetic) |
| master | `ScriptedMaster` M (scenarios.md §1.1) | `ScriptedMaster` M with C and E offers and a `b_ready` list |
| slave | `ScriptedSlave` S, `a_ready` HIGH unless stated; S answers a request as scripted: a dataless response or a Get's AccessAckData at (first beat accepted) + 1, a Put's AccessAck at (last beat accepted) + 1, an atomic's response beat k at (request beat k accepted) + 1 | `ScriptedSlave` S with B offers; Grants at (Acquire accepted) + 1 unless stated |
| monitor | fatal mode; the test asserts exit status 2 and the first (only) report line | same |
| reset | `reset_cycles = 100` | same |
| `t0` | the run cycle at which the mutant's scenario begins (100 unless stated); every "fires at" below is `t0 + k` | same, after the setup phase of §1.3 |

The mutant is always the behaviour of one agent; the other agent is conforming. "Nothing else" means
the first report (plan §0.2 evaluation order) is the expected id. Where a second rule would also hold in
report mode, the row says so.

### 1.2 Notation

`Get(src=0, size=3, addr=0x100)`: a Get with those fields, mask derived (all active lanes), param 0,
corrupt 0. `+k` is `t0 + k`. Beats of a burst are written `beat 0`, `beat 1`.

### 1.3 TL-C setup phases

| macro | cycles (relative to the setup start s0) | effect |
|---|---|---|
| SETUP-B(X) | s0: M AcquireBlock NtoB src 0 on block X (addr X, size 6); s0+1: S GrantData toB sink 0 (one beat on w=64); s0+2: M GrantAck sink 0 | `perm[X] = B`, nothing pending; `t0 = s0 + 4` |
| SETUP-T(X) | as SETUP-B with NtoT / GrantData toT | `perm[X] = T` |
| SETUP-N | nothing | `perm = N` |

Two blocks in one setup (M-ID-2) run the macros back to back with sources 0 and 1 (s0 and s0 + 1, the
Grants at s0 + 2 and s0 + 3, the GrantAcks at s0 + 4 and s0 + 5; `t0 = s0 + 8`).

## 2. TL-UL mutants (T2)

Link level UL unless stated.

| rule | mutant (the wrong behaviour) | scenario | fires at | notes |
|---|---|---|---|---|
| M-RESET-1 | master drives `a_valid` HIGH during reset | M offers Get(src=0, size=3, addr=0x100) at run cycle 50 (reset asserted 0..99) | run cycle 50 | absolute, not `t0` |
| M-RESET-2 | configuration with `reset_cycles=10`, standalone | `tlsim` with that system file | elaboration (no cycle) | exit status 2 before cycle 0 |
| M-LEVEL-1 | TL-UL master sends an atomic | M offers ArithmeticData(param=4 ADD, src=0, size=3, addr=0x100, data 1) at +0 | +0 | offer-time check |
| M-LEVEL-2 | TL-UL slave answers a Get with HintAck | M Get(src=0,size=3,addr=0x100) at +0; S offers HintAck src 0 size 3 at +1 | +1 | M-RESP-1 would also hold; M-LEVEL-2 is first in order |
| M-LEVEL-4 | master drives a C beat on a TL-UL link | M offers Release(BtoN, src=0, size=6, addr=0x100) on C at +0 | +0 | the elaboration capability check is bypassed by the scripted master |
| M-LEVEL-5 | TL-UL master sends a burst | M offers PutFullData(src=0, size=4, addr=0x100) beat 0 at +0 | +0 | 2^4 = 16 > w = 8; `max_size=6` so M-ALIGN-3 stays quiet |
| M-PARAM-1 | Get with a non-zero param | M offers Get(param=1, src=0, size=3, addr=0x100) at +0 | +0 | |
| M-PARAM-5 | slave answers with a non-zero param | M PutFullData(src=0,size=3,addr=0x100) at +0; S offers AccessAck(param=1, src 0, size 3) at +1 | +1 | |
| M-ALIGN-1 | misaligned address | M offers Get(src=0, size=3, addr=0x104) at +0 | +0 | |
| M-ALIGN-2 | size does not fit z | M offers Get(src=0, size=8, addr=0x100) at +0, link `z=3` | +0 | the Msg field holds the integer 8 |
| M-ALIGN-3 | size above the slave's maximum | M offers Get(src=0, size=7, addr=0x0) at +0 (`max_size=6`), link level UH | +0 | M-LEVEL-5 would fire first on a UL link, so this one runs at UH |
| M-ALIGN-4 | address does not fit a | M offers Get(src=0, size=3, addr=0x1_0000_0000) at +0, link `a=32` | +0 | |
| M-ALIGN-5 | burst crosses the slave's region | link `z=4 max_size=12`, level UH, slave region [0x1000, 0x1800); M offers Get(src=0, size=12, addr=0x1000) at +0 | +0 | [0x1000, 0x2000) leaves the region at 0x1800 |
| M-ALIGN-6 | source does not fit o | M offers Get(src=4, size=3, addr=0x100) at +0, link `o=2` | +0 | |
| M-MASK-1 | mask HIGH on inactive lanes | M offers PutFullData(src=0, size=2, addr=0x104, mask=0xff) at +0 | +0 | active lanes 4..7; mask has lanes 0..3 HIGH |
| M-MASK-2 | PutFullData with a partial mask on a full beat | M offers PutFullData(src=0, size=3, addr=0x100, mask=0x0f) at +0 | +0 | the plan's example |
| M-MASK-2 (b) | dataless message larger than the bus with a partial mask | link level UH; M offers Get(src=0, size=5, addr=0x100, mask=0x0f) at +0 | +0 | §4.5 "all bits HIGH" |
| M-MASK-3 | PutPartialData with a mask bit outside its region | M offers PutPartialData(src=0, size=2, addr=0x104, mask=0x11) at +0 | +0 | bit 0 is outside lanes 4..7; M-MASK-1 does not apply to PutPartialData |
| M-RSV-1 | corrupt on a dataless message | M offers Get(src=0, size=3, addr=0x100, corrupt=1) at +0 | +0 | |
| M-ERR-1 | denied set on an A message | M offers Get(src=0, size=3, addr=0x100, denied=1) at +0 | +0 | model-field check |
| M-ERR-2 | denied AccessAckData with one beat not corrupt | link level UH; M Get(src=0, size=5, addr=0x100) at +0; S offers AccessAckData(src 0, size 5, denied=1) beats at +1..+4 with corrupt = 1, 1, 0, 1 | +3 | beat 2 is offered at +3; the plan's example |
| M-BURST-5 | slave retracts a response and never re-offers it | M Get(src=0, size=3, addr=0x100) at +0 with `d_ready` LOW at +1; S offers AccessAckData src 0 at +1 (rejected) and nothing afterwards; watchdog off (`progress_bound=0`); run ends at +50 | +50 (end of run) | M-RESP-7 stays quiet by definition (the response was offered) |
| M-RESP-1 | wrong response type | M Get(src=0, size=3, addr=0x100) at +0; S offers AccessAck src 0 size 3 at +1 | +1 | |
| M-RESP-2 | unsolicited response | S offers AccessAck(src 1, size 3) at +0 with no request outstanding; M `d_ready` HIGH | +0 | the same scenario with M's Get src 1 at +1 is the "response one cycle before its request" case and fires the same rule at +0 |
| M-RESP-6 | response size differs from the request's | M Get(src=0, size=3, addr=0x100) at +0; S offers AccessAckData(src 0, size 2) at +1 | +1 | |
| M-RESP-7 | request never answered | M Get(src=0, size=3, addr=0x100) at +0; S never answers; watchdog off; run ends at +50 | +50 (end of run) | with the watchdog on, rule 3ii trips first at +0 + `progress_bound` (TL-PROGRESS below) |
| M-ID-1 | source reused while outstanding | M Get(src=2, size=3, addr=0x100) at +0; S answers at +1; M offers a second Get(src=2, size=3, addr=0x108) at +1 | +1 | at +1 the first request is still outstanding (its response is accepted in that same cycle; plan §0.3 of the rules file) |

## 3. TL-UH mutants (T2 for the monitor, T3 for the agents)

Link level UH.

| rule | mutant | scenario | fires at | notes |
|---|---|---|---|---|
| M-LEVEL-3 | unassigned C opcode (on a TL-C link) | see §4 | | listed under TL-C |
| M-PARAM-2 | ArithmeticData param out of range | M offers ArithmeticData(param=5, src=0, size=3, addr=0x100) at +0 | +0 | |
| M-PARAM-3 | LogicalData param out of range | M offers LogicalData(param=4, src=0, size=3, addr=0x100) at +0 | +0 | |
| M-PARAM-4 | Intent with an unassigned param | M offers Intent(param=2, src=0, size=3, addr=0x100) at +0 | +0 | the B-channel variant (Intent param 5 on B) is a TL-C mutant, §4 |
| M-BURST-1 | beat index repeated after acceptance | M offers PutFullData(src=0, size=5, addr=0x100) beat 0 at +0, beat 1 at +1, beat 1 again at +2 (S accepts everything) | +2 | expected index 2 |
| M-BURST-2 | address advanced per beat | M offers PutFullData(src=0, size=5, addr=0x100) beat 0 at +0; beat 1 at +1 with `address=0x108` | +1 | the classic error; the wire address is constant (plan §1.4) |
| M-BURST-3 | another message interleaved in a burst | M offers PutFullData(src=0, size=5, addr=0x100) beats 0, 1 at +0, +1; Get(src=1, size=3, addr=0x200) at +2; beats 2, 3 at +3, +4 | +2 | the plan's example "after beat 1" |
| M-BURST-4 | burst abandoned | M offers PutFullData(src=0, size=5, addr=0x100) beat 0 at +0 (accepted), then `a_valid` LOW forever; watchdog off; run ends at +50 | +50 (end of run) | with the watchdog on: rule 2 at +0 + `progress_bound` (TL-PROGRESS §6.2 is the two-link form) |

## 4. TL-C mutants (T6)

Link `w=64 a=32 z=3 o=2 i=2 level=C max_size=6 block_bytes=64 c_ordered=1`. X = block at 0x1000,
Y = block at 0x2000. All Acquires, Probes and Releases use size 6 and the block's address. `t0` follows
the setup phase (§1.3).

| rule | setup | mutant | scenario | fires at | notes |
|---|---|---|---|---|---|
| M-LEVEL-3 | SETUP-N | unassigned C opcode | M offers a C beat with opcode 3 (src 0, size 6, addr X) at +0 | +0 | |
| M-ALIGN-7 | SETUP-N | sink does not fit i | M AcquireBlock(NtoB, src=0, X) at +0; S offers GrantData(toB, src 0, sink 4) at +1, link `i=2` | +1 | |
| M-PARAM-4 (B) | SETUP-B(X) | CMO on channel B | S offers Intent(param=5 CBOInval, src 0, size 6, addr X) on B at +0 | +0 | §9.5.8: prefetch hints only on B |
| M-PARAM-6 | SETUP-N | Acquire param not a Grow | M offers AcquireBlock(param=3, src=0, X) at +0 | +0 | |
| M-PARAM-7 | SETUP-B(X) | Probe param not a Cap | S offers ProbeBlock(param=3, src 0, X) at +0 | +0 | |
| M-PARAM-8 | SETUP-T(X) | ProbeAckData NtoN | S ProbeBlock(toN, src 0, X) at +0; M offers ProbeAckData(NtoN, src 0, X, 1 beat) at +1 | +1 | M-PERM-2 would also hold (from T); M-PARAM-8 is first |
| M-PARAM-9 | SETUP-N | Grant toN | M AcquireBlock(NtoB, src=0, X) at +0; S offers Grant(toN, src 0, sink 0) at +1 | +1 | plan §8 decision 7; the same with `denied=1` also fires (toN never encoded) |
| M-PARAM-10 | SETUP-B(X) | ReleaseAck with a param | M Release(BtoN, src=0, X) at +0; S offers ReleaseAck(param=1, src 0, size 6) at +1 | +1 | |
| M-PARAM-11 | SETUP-N | Grant below the request | M AcquireBlock(NtoT, src=0, X) at +0; S offers Grant(toB, src 0, sink 0, denied=0) at +1 | +1 | with `denied=1` no violation (exempt) |
| M-RSV-2 | SETUP-B(X) | ReleaseAck denied | M Release(BtoN, src=0, X) at +0; S offers ReleaseAck(src 0, size 6, denied=1) at +1 | +1 | |
| M-ERR-3 | SETUP-N | master treats a denied Grant as success | M AcquireBlock(NtoT, src=0, X) at +0; S Grant(toT, src 0, sink 0, denied=1) at +1; M GrantAck sink 0 at +2; M offers Release(TtoN, src=0, X) at +3 | +3, as **M-PERM-2** | M-ERR-3 has no report of its own (rules file §7); the exemption keeps `perm[X] = N`, so the Release "from T" is the violation |
| M-RESP-3 | SETUP-B(X) | ProbeAck with no Probe outstanding | M offers ProbeAck(BtoB, src 0, X) at +0 | +0 | |
| M-RESP-4 | SETUP-T(X) | ProbeAckData answering ProbePerm | S ProbePerm(toN, src 0, X) at +0; M offers ProbeAckData(TtoN, src 0, X) at +1 | +1 | |
| M-RESP-5 | SETUP-N | GrantAck with no Grant | M offers GrantAck(sink 1) on E at +0 | +0 | |
| M-ID-2 | SETUP-B(X), SETUP-B(Y) | C source reused while a Release is outstanding | M Release(BtoN, src=0, X) at +0; M Release(BtoN, src=0, Y) at +1; S ReleaseAcks at +2 and +3 | +1 | the namespace is C's own: an outstanding A source 0 would not conflict |
| M-ID-3 | SETUP-B(X) | B (source, address) reused | S ProbeBlock(toN, src 0, X) at +0 and again ProbeBlock(toN, src 0, X) at +1; M answers at +2 | +1 | M-CONC-9 holds too; M-ID-3 is first in order |
| M-ID-4 | SETUP-N | sink reused while its GrantAck is pending | M AcquireBlock(NtoB, src=0, X) at +0 and AcquireBlock(NtoB, src=1, Y) at +1; S Grant(toB, src 0, sink 0) at +2 and Grant(toB, src 1, sink 0) at +3; M GrantAck sink 0 at +4 | +3 | |
| M-CONC-1 | SETUP-N | second Acquire on a block with a Grant pending | M AcquireBlock(NtoB, src=0, X) at +0; S Grants at +4; M AcquireBlock(NtoT, src=1, X) at +1 | +1 | the plan's example; with `allow_acquire_under_acquire=1` no violation (distinct source, M-PERM-1 holds since `perm[X]` is still N) |
| M-CONC-2 | SETUP-N | Acquire while an uncached access is pending | M Get(src=0, size=3, addr=X) at +0; S answers at +4; M AcquireBlock(NtoB, src=1, X) at +1 | +1 | |
| M-CONC-3 | SETUP-N | Get while an Acquire is pending | M AcquireBlock(NtoB, src=0, X) at +0; S Grants at +4; M Get(src=1, size=3, addr=X) at +1 | +1 | |
| M-CONC-4 | SETUP-B(X) | Release while a Grant is pending | M AcquireBlock(BtoT, src=0, X) at +0; S Grants at +4; M Release(BtoN, src=0, X) at +1 | +1 | the C source 0 is free (own namespace) |
| M-CONC-5 | SETUP-T(X) | ProbeAck while a Release is pending | S ProbeBlock(toN, src 0, X) at +0; M ReleaseData(TtoN, src=0, X) at +1 (legal, Figure 30); M offers ProbeAck(TtoN, src 0, X) at +2; S ReleaseAck at +4 | +2 | the plan's example; the conforming master holds the ProbeAck until +5 |
| M-CONC-6 | SETUP-B(X) | Acquire while a Release is pending | M Release(BtoN, src=0, X) at +0; S ReleaseAck at +4; M AcquireBlock(NtoB, src=0, X) at +1 | +1 | `perm[X]` is already N after the Release, so M-PERM-1 holds; the A source 0 is free |
| M-CONC-7 | SETUP-B(X) | Grant while a Probe is pending | M AcquireBlock(BtoT, src=0, X) at +0; S ProbeBlock(toN, src 0, X) at +1 (its own eviction); M does not answer before +5; S offers Grant(toT, src 0, sink 0) at +2 | +2 | Figure 30's rule: Grant only after every ProbeAck |
| M-CONC-8 | SETUP-N | Probe while a GrantAck is pending | M AcquireBlock(NtoB, src=0, X) at +0; S Grant(toB, src 0, sink 0) at +1; M GrantAck at +4; S offers ProbeBlock(toN, src 0, X) at +2 | +2 | |
| M-CONC-9 | SETUP-T(X) | second Probe on the block | S ProbeBlock(toB, src 0, X) at +0 and ProbeBlock(toN, src 1, X) at +1; M answers at +2 | +1 | distinct source so M-ID-3 stays quiet |
| M-CONC-10 | SETUP-N | AccessAckData while a ProbeBlock is pending | M Get(src=0, size=3, addr=X) at +0; S ProbeBlock(toN, src 0, X) at +1 (a broadcast manager probing the requester itself); S offers AccessAckData(src 0, size 3) at +2; M ProbeAck(NtoN) at +4 | +2 | with ProbePerm instead of ProbeBlock no violation (ProbeAck carries no data) |
| M-CONC-11 | SETUP-T(X), link `c_ordered=0` | ProbeAckData TtoT on an unordered C | S ProbeBlock(toT, src 0, X) at +0; M offers ProbeAckData(TtoT, src 0, X) at +1 | +1 | with `c_ordered=1` no violation |
| M-PERM-1 | SETUP-N | Acquire BtoT from N | M offers AcquireBlock(BtoT, src=0, X) at +0 | +0 | variant: SETUP-B(X) then AcquireBlock(NtoB) fires the same rule |
| M-PERM-2 | SETUP-B(X) | Release TtoN from B | M offers Release(TtoN, src=0, X) at +0 | +0 | |
| M-PERM-3 | SETUP-T(X) | ProbeAck does not satisfy the cap | S ProbeBlock(toN, src 0, X) at +0; M offers ProbeAck(TtoB, src 0, X) at +1 | +1 | |

## 5. Coverage

| group | rules | mutants | not mutated |
|---|---|---|---|
| M-RESET | 2 | 2 | |
| M-LEVEL | 5 | 5 | |
| M-PARAM | 11 | 12 (M-PARAM-4 twice: A and B) | |
| M-ALIGN | 7 | 7 | |
| M-MASK | 3 | 4 (M-MASK-2 twice: data and dataless) | |
| M-RSV | 2 | 2 | |
| M-ERR | 3 | 3 (M-ERR-3 fires as M-PERM-2) | |
| M-BURST | 5 | 5 | |
| M-RESP | 7 | 7 | |
| M-ID | 4 | 4 | |
| M-CONC | 11 | 11 | |
| M-PERM | 3 | 3 | |
| total | 63 | 65 | |

T2 runs §2 and §3 (34 mutants, of which M-RESET-2 is an elaboration test); T6 runs §4 (31 mutants).

## 6. TL-PROGRESS: the §5.2 examples (T4)

The watchdog (plan §3.6) keeps a timer per presented beat, per burst in progress and per received
request; a timer past `progress_bound` trips, the dump names the link, the message, the rule it points
at (3i, 2, 3ii) and the bound, and the run stops. It is a diagnostic: the bound is a declaration of
the configuration, and the dump prints every timer past the bound so that a reader can apply rule 3's
exemption. Default bound formula (plan §3.6): (slowest slave latency + deepest queue + largest burst in
beats) x number of masters.

All four scenarios: `w=8 a=32 z=3 o=2 i=0 level=UH`, `reset_cycles=100`, `data_mode=none`, every link
monitored (0 violations in all four: none of them breaks a per-link rule; that is the point of §5).

### 6.1 Periodic forwarding master (non-conforming): watchdog names rule 3ii

Agents: `ScriptedMaster` M -> link L1 -> `Forwarder` F (mutant) -> link L2 -> `ScriptedSlave` S.
- F: a two-link agent that forwards A beats from L1 to L2 and D beats back; the mutant raises
  `a_valid` on L2 only on even run cycles (the odd cycles retract the beat), §5.2 example 1.
- S: `a_ready` HIGH only on odd run cycles (a legal "periodic readiness", §5.2 example 2); answers in the
  cycle after acceptance.
- M: Get(src=0, size=3, addr=0x100) at +0, accepted by F at +0 (F's `a_ready` HIGH).
- F offers the forwarded Get on L2 from +1 on even cycles only; S is ready only on odd cycles: never
  accepted. F's beat on L2 is never *presented* (it is retracted every other cycle, §5.1), so no 3i timer
  runs on L2; the request received on L1 at +0 is never answered.
- `progress_bound = 16` (formula: (1 + 1 + 1) x 1 = 3; 16 leaves a margin for S's period of 2).
- Verdict: trip at +16, `rule 3ii`, link L1, agent F, message Get src 0 received at +0, bound 16.
- Control: the same with F conforming (valid held HIGH) completes at +3 (accepted on L2 at the first
  odd cycle, answered, forwarded), no trip.

### 6.2 Withholding beats of a received message (non-conforming): watchdog names rule 2

Agents: `ScriptedMaster` X (mutant, two master links) -> links L1 and L2 -> `SlowArbiter` Y
(conforming, §5.2 example 4) -> link L3 -> `RamSlave` Z (`latency 2`).
- Y: idle, it selects a valid request from L1 or L2; while a request is selected it lowers `a_ready`
  on the other link and connects the selected link to L3 for A and D; it returns to idle once the
  response's last beat has been accepted by the selected link. (Y is also the §6.3 agent.)
- X: PutFullData(src=0, size=5, addr=0x100) beat 0 on L1 at +0, accepted (Y selects L1); X then starts
  PutFullData(src=0, size=5, addr=0x200) beat 0 on L2 at +1 and holds `a_valid` LOW on L1 until L2's
  beat is accepted. Y keeps L2's `a_ready` LOW while busy with L1 (legal under rule 3: Y has an in-flight
  message on its higher-priority forwarding link), so L2 never accepts and L1 never resumes.
- `progress_bound = 32` (formula: (2 + 1 + 4) x 2 = 14; 32 gives margin).
- Verdict: trip at +32, `rule 2`, link L1, agent X, burst PutFullData src 0 "1 of 4 beats accepted, no
  further beat accepted since +0", bound 32. The 3i timer of L2 (beat presented at +1 and rejected)
  reaches the bound one cycle later, at +33, and is printed in the dump as "also past the bound"; the
  verdict is the earliest trip (ties broken by rule order 2, 3i, 3ii).
- Control: the same with X conforming (it finishes burst A first) completes both Puts, no trip, and Y
  rejects L2 for 4 + 2 + 1 cycles, within the bound.

### 6.3 Very slow arbiter (conforming): no trip

Agents: `FuzzMaster` M1 and `FuzzMaster` M2 (Get and PutFullData, sizes 3..5, 4 requests each,
one outstanding at a time) -> links L1, L2 -> `SlowArbiter` Y (as in §6.2) -> link L3 -> `RamSlave`
Z (`latency 4`).
- Longest legal wait of a presented beat: one whole transaction of the other master, burst 4 beats +
  latency 4 + response 4 beats + Y's two idle transitions, about 14 cycles.
- `progress_bound = 32` (formula: (4 + 1 + 4) x 2 = 18).
- Verdict: no trip; all 8 requests answered; the transcript shows Y serving L1 and L2 alternately
  (whichever is valid when Y is idle; ties to L1).
- Control (the bound is a declaration, not a verdict): `progress_bound = 8` trips `rule 3i` on L2 at
  the first long wait although every agent conforms; the dump prints the bound, which is what makes
  the trip readable as "bound too small", not "non-conforming".

### 6.4 Waiting for a refresh (conforming): no trip

Agents: `FuzzMaster` M (Get and PutFullData, sizes 3..5, 8 requests, 2 outstanding) -> link L1 ->
`RamSlave` Z with `refresh_period 50`, `refresh_cycles 20` (every 50 cycles it lowers `a_ready` and
`d_valid`-side service for 20 cycles unconditionally, §5.2 example 2), `latency 4`.
- Longest legal wait: refresh 20 + latency 4 + a 4-beat burst ahead = 28.
- `progress_bound = 40` (formula with the refresh counted as queue depth: (4 + 20 + 4) x 1 = 28).
- Verdict: no trip; all 8 requests answered; the stall counter of `TLStats.csv` (valid && !ready on A)
  is at least 20 per refresh window in which a beat was presented.
- Control: `progress_bound = 16` trips `rule 3i` on L1 during the first refresh window in which a beat is
  presented, with the bound printed.

### 6.5 Expected watchdog output format

```
WATCHDOG rule <2|3i|3ii> link=<name> agent=<name> cycle=<n> bound=<progress_bound> msg=<opcode src=<o> addr=<hex>> since=<cycle> <text>
  also past the bound: <same fields>...
```

The run exits with status 2 after the dump (plan §3.6: it never recovers).
