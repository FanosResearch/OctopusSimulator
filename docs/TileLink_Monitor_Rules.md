# TileLink conformance monitor: rule catalogue

Status: T2/T4 design input, written 2026-10-21 against `docs/TileLink_Plan.md` (plan §3.6 groups, plan §1.4
matching, plan §8 decisions 7 and 8) and *TileLink Specification 1.9.3*. "§x" is the spec, "plan §x" the
plan. One row per atomic rule; the T2 implementer codes from this file rule by rule. The TL-NEG mutant
for every rule id is in `docs/TileLink_Negative_Tests.md`.

## 0. Conventions

### 0.1 Monitor instance and parameters

One `Monitor` per link (plan §3.4). It knows the link's `w a z o i level`, the slave's declared
`max_size`, the address regions the slave serves (optional; from the system file's address map), the
monitor parameters below, and the kernel's reset state.

| parameter | default | used by |
|---|---|---|
| `block_bytes` | 64 | M-CONC, M-PERM: block = address >> log2(block_bytes); a message covers every block that intersects [address, address + 2^size) |
| `allow_acquire_under_acquire` | 0 | M-CONC-1 (plan §1.4 "one Acquire per block", strict reading; 1 = the §9.2 write-miss-under-read-miss relaxation) |
| `c_ordered` | 1 | M-CONC-11 (C channel delivers in order per address on this link; 1 for a direct link and for the `Xbar`, which is FIFO per output) |
| `mode` | fatal | fatal: first violation exits with status 2; report: count and continue |

### 0.2 When a rule is checked

| moment | meaning in the three-phase cycle (plan §3.2) |
|---|---|
| **offer** | phase 1, every cycle in which the channel's `valid` is HIGH, whether or not the beat is accepted. §4.1: with valid HIGH the fields must be a syntactically correct beat. Reported once per (message, beat, rule), so a beat re-offered after a rejection is not reported twice |
| **accept** | phase 3, the cycle in which `valid && ready`; "first beat" and "last beat" are the first and last accepted beat of the message |
| **end of run** | the last cycle of the simulation, before the report |
| **reset** | every cycle in which reset is asserted |
| **elaboration** | before cycle 0, from the configuration |

Inside Octopus (plan §5.2) "accept" is the enqueue into the `TLPort` receive queue.

**Evaluation order within one beat**, which is the order of this document: M-RESET, M-LEVEL, M-PARAM,
M-ALIGN, M-MASK, M-RSV, M-ERR, M-BURST, M-RESP, M-ID, M-CONC, M-PERM. In fatal mode the first failing rule
in this order is the one reported; the TL-NEG criterion "exactly that rule id" is applied to that report.

### 0.3 Per-link state the rules observe

| state | contents | written by |
|---|---|---|
| `lock[ch]` | the message in progress on channel ch (A, B, C, D carry bursts; E never): handle, control fields of the first beat, beats accepted so far, beats expected | M-BURST |
| `last_offer[ch]` | the beat offered in the previous cycle and whether it was accepted (for retraction tracking) | M-BURST-5 |
| `outA[source]` | outstanding A requests: opcode, param, size, address, cycle of first beat, response beats received | M-RESP, M-ID-1, M-CONC |
| `outC[source]` | outstanding C requests (Release, ReleaseData), own namespace (§6.4) | M-RESP, M-ID-2, M-CONC |
| `outB[(source, address)]` | outstanding B requests: opcode, param, size | M-RESP, M-ID-3, M-CONC |
| `grants[sink]` | unanswered Grant/GrantData: source, address, param, denied | M-RESP-5, M-ID-4, M-CONC-8 |
| `perm[block]` | the link's master's tracked permission on the block: N, B or T (TL-C only) | M-PERM |
| `pend[block]` | per-block pending sets (TL-C only): `acq` (Acquires outstanding, by source), `acc` (uncached A accesses outstanding, by source), `rel` (Release[Data] outstanding, by source), `probe` (Probes outstanding, by source, with opcode), `gack` (Grants awaiting GrantAck, by sink) | M-CONC |
| `retracted` | D or C response beats offered, rejected and then retracted, not yet accepted | M-BURST-5 |

**Outstanding** (an entry of `outA`, `outC`, `outB`, `grants`, or a member of a `pend` set) runs from the
cycle the request's first beat is accepted up to and including the cycle the response's **last** beat is
accepted; the identifier is reusable from the next cycle. §6.4 says "answered" ("including the cycle on
which the response is received"), and §5.1 defines received as the first beat accepted; the plan takes
the response's last beat, which is what rocket-chip's monitor does and what the burst lock already
tracks. Recorded as a decision (stricter than the literal wording by the length of a response burst).

### 0.4 Derived quantities

| name | definition | spec |
|---|---|---|
| `has_data(ch, opcode)` | A/B: PutFullData, PutPartialData, ArithmeticData, LogicalData; C: AccessAckData, ProbeAckData, ReleaseData; D: AccessAckData, GrantData; E: never | Tables 12, 13 |
| `beats(ch, opcode, size)` | has_data ? max(1, 2^size / w) : 1 | §4, §8.3 |
| `active(size, address)` | lanes of one beat: if 2^size >= w, all w lanes; else lanes [address mod w, address mod w + 2^size) | §4.5 |
| `beat_address(k)` | address + k * w, the effective address of beat k of a burst; never a wire field (plan §1.4) | §4.1 |
| `block_set(address, size)` | blocks intersecting [address, address + 2^size) | plan |
| namespace of a D opcode | AccessAck, AccessAckData, HintAck, Grant, GrantData answer `outA`; ReleaseAck answers `outC` | §6.4, Table 14 |

### 0.5 Violation message

Every report carries: `rule-id`, spec section, link name, cycle, channel, message (opcode name, param,
size, source, sink, address, beat index), and the rule-specific text in the "message" column below. Format:

```
<rule-id> <spec §> link=<name> cycle=<n> ch=<A..E> msg=<opcode>[param=<p> size=<s> src=<o> sink=<i> addr=<hex> beat=<k>]: <text>
```

## 1. M-RESET (§3.2.2)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-RESET-1 | §3.2.2 | While reset is asserted, a_valid, c_valid and e_valid are LOW (master side) and b_valid and d_valid are LOW (slave side) | `valid` of every channel, kernel reset flag | reset | `valid HIGH on <ch> during reset (cycle <n> of <reset_cycles>)` | UL |
| M-RESET-2 | §3.2.2 | Standalone, reset is asserted for at least 100 cycles; `reset_cycles` < 100 is refused (0 is allowed only inside Octopus, plan §3.2) | configuration | elaboration | `reset_cycles=<n> < 100 (standalone minimum)` | UL |

## 2. M-LEVEL (§1.1 Table 1, §6.2 Tables 12 and 13, §7, §7.2)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-LEVEL-1 | Tables 12, 13; §7, §8, §9 | A opcode is legal at the link's level: TL-UL {0 PutFullData, 1 PutPartialData, 4 Get}; TL-UH adds {2 ArithmeticData, 3 LogicalData, 5 Intent}; TL-C adds {6 AcquireBlock, 7 AcquirePerm} | a_opcode, link level | offer | `A opcode <n> not legal at <level>` | UL |
| M-LEVEL-2 | Tables 12, 13 | D opcode is legal at the link's level: TL-UL {0 AccessAck, 1 AccessAckData}; TL-UH adds {2 HintAck}; TL-C adds {4 Grant, 5 GrantData, 6 ReleaseAck}; 3 and 7 are never legal | d_opcode, link level | offer | `D opcode <n> not legal at <level>` | UL |
| M-LEVEL-3 | Table 13 | B opcode is in 0..7 (all eight are legal on a TL-C link: 0..5 are forwarded Access/Hint messages, §9.5, which the monitor accepts although no agent of the plan emits them, plan §2); C opcode is in {0, 1, 2, 4, 5, 6, 7} (3 is unassigned); E has no opcode | b_opcode, c_opcode | offer | `<ch> opcode <n> unassigned` | C |
| M-LEVEL-4 | §3.4, §3.5, §3.7, Table 1 | A TL-UL or TL-UH link has no B, C or E channel: valid HIGH on one of them is a violation (the link object has no such channel; an agent that drives one is caught at elaboration by the capability check of plan §3.4, this rule is the runtime backstop) | b/c/e_valid, link level | offer | `beat on <ch> of a <level> link` | UL |
| M-LEVEL-5 | §7 "in TL-UL every message fits within a single beat", §7.2.1 to §7.2.5 "a_size/d_size cannot be larger than the width of the physical data bus" | On a TL-UL link, 2^size <= w on A and on D | a_size, d_size, w, link level | offer | `size <s> (2^s > w=<w>) on a TL-UL link` | UL |

## 3. M-PARAM (§7.2, §8.2, §9.3, §9.5, Tables 23, 25, 27, 31, 51; plan §8 decision 7)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-PARAM-1 | §7.2.1 to §7.2.3, §9.5.1 to §9.5.3, Tables 16 to 18, 44 to 46 | Get, PutFullData and PutPartialData carry param 0 (reserved), on A and on B | a/b_param | offer | `<opcode> param <p> != 0 (reserved)` | UL |
| M-PARAM-2 | §8.2.1 Table 23, §9.5.6 | ArithmeticData param is in {0 MIN, 1 MAX, 2 MINU, 3 MAXU, 4 ADD}, on A and on B | a/b_param | offer | `ArithmeticData param <p> not in 0..4` | UH |
| M-PARAM-3 | §8.2.2 Table 25, §9.5.7 | LogicalData param is in {0 XOR, 1 OR, 2 AND, 3 SWAP}, on A and on B | a/b_param | offer | `LogicalData param <p> not in 0..3` | UH |
| M-PARAM-4 | §8.2.3 Table 27; §9.5.8 Table 51 | Intent on A carries param in {0 PrefetchRead, 1 PrefetchWrite, 5 CBOInval, 6 CBOClean, 7 CBOFlush}; Intent on B carries param in {0, 1} only (no CMOs on B) | a/b_param | offer | `Intent param <p> not legal on <ch>` | UH |
| M-PARAM-5 | §7.2.4, §7.2.5, §8.2.4, §9.5.4, §9.5.5, §9.5.9, Tables 19, 20, 28, 47, 48, 53 | AccessAck, AccessAckData and HintAck carry param 0, on D and on C | c/d_param | offer | `<opcode> param <p> != 0 (reserved)` | UL |
| M-PARAM-6 | §9.3.1, §9.3.2, Tables 32, 33 | AcquireBlock and AcquirePerm carry a Grow param: {0 NtoB, 1 NtoT, 2 BtoT} | a_param | offer | `<opcode> param <p> not a Grow` | C |
| M-PARAM-7 | §9.3.3, §9.3.4, Tables 34, 35 | ProbeBlock and ProbePerm carry a Cap param: {0 toT, 1 toB, 2 toN} | b_param | offer | `<opcode> param <p> not a Cap` | C |
| M-PARAM-8 | §9.3.5, §9.3.6, §9.3.10, §9.3.11, Tables 36, 37, 41, 42 | ProbeAck carries a Prune or Report param: {0 TtoB, 1 TtoN, 2 BtoN, 3 TtoT, 4 BtoB, 5 NtoN}; ProbeAckData, Release and ReleaseData carry the same set without NtoN: {0, 1, 2, 3, 4} | c_param | offer | `<opcode> param <p> not legal (NtoN forbidden on <opcode>)` | C |
| M-PARAM-9 | §9.3.7, §9.3.8, Tables 38, 39; plan §8 decision 7 | Grant and GrantData carry a Cap param in {0 toT, 1 toB}: toN (2) is never encoded, denied or not; 3 is unassigned (the spec's Tables 38 and 39 list the whole Cap category; the exclusion of toN is the plan's interoperability rule, rocket-chip's) | d_param, d_denied | offer | `<opcode> param <p> (toN or unassigned) on a Grant` | C |
| M-PARAM-10 | §9.3.12 Table 43 | ReleaseAck carries param 0 | d_param | offer | `ReleaseAck param <p> != 0 (reserved)` | C |
| M-PARAM-11 | §9.3.7, §9.3.8 "may exceed those requested"; plan §8 decision 7 | A Grant or GrantData with denied LOW meets or exceeds the Acquire it answers: Acquire NtoB -> toB or toT; NtoT -> toT; BtoT -> toT. A denied Grant is exempt (its param has no meaning, §9.3.7, but must still satisfy M-PARAM-9) | d_param, d_denied, `outA[d_source].param` | first beat accept (after M-RESP-1 has matched the request) | `Grant cap <cap> below Acquire <grow> (src <o>)` | C |

Width note: on the wire a/b/c_param is 3 bits and d_param is 2 bits, so values above 7 (3 on D) cannot
exist; the simulator carries integers and the range checks above reject them too.

## 4. M-ALIGN (§4.5, §3.3 to §3.7, §6.3, Table 4)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-ALIGN-1 | §4.5 "(address & (2^size - 1)) = 0 always holds", Tables 6 to 8, §9.3.x "must be aligned" | On A, B and C, address & (2^size - 1) == 0 | address, size | offer | `address <hex> not aligned to size <s>` | UL |
| M-ALIGN-2 | Table 4 (z), Tables 6 to 9 | size < 2^z on A, B, C and D (the size field has z bits, so sizes 0..2^z - 1) | size, z | offer | `size <s> does not fit z=<z> bits` | UL |
| M-ALIGN-3 | §6.3 "Masters must not generate operations larger than slaves support" | On A, size <= the slave's declared `max_size` (TL-C: Acquire, Intent/CMO included) | a_size, slave `max_size` | offer | `size <s> > slave max_size <m>` | UL |
| M-ALIGN-4 | Table 4 (a), Tables 6 to 8 | address < 2^a on A, B and C (a = 0 means the address is always 0) | address, a | offer | `address <hex> does not fit a=<a> bits` | UL |
| M-ALIGN-5 | §6.3 "burst operations must not under- or over-run the boundaries of the slave" | On A, [address, address + 2^size) lies within one region of the slave's address map (skipped when the link has no address map) | a_address, a_size, address map | offer | `operation [<lo>,<hi>) crosses slave region boundary <b>` | UL |
| M-ALIGN-6 | Table 4 (o), §6.4 | source < 2^o on A, B, C and D | source, o | offer | `source <o> does not fit o=<o> bits` | UL |
| M-ALIGN-7 | Table 4 (i), §6.4 | sink < 2^i on D and E | sink, i | offer | `sink <i> does not fit i=<i> bits` | C |

## 5. M-MASK (§4.5, §7.2, §8.2, §9.3, §9.5)

Mask exists on A and B only (Tables 6, 7). C, D and E carry no mask: nothing to check there. For a burst
beat 2^size >= w, so `active` is every lane and the rules below apply to every beat with the same result.

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-MASK-1 | §4.5 "the mask must always be LOW for all inactive byte lanes" | On A and B, for every message except PutPartialData, mask & ~active(size, address) == 0 | mask, size, address, w | offer | `mask <hex> HIGH on inactive lane(s) <hex> (size <s>, address <hex>)` | UL |
| M-MASK-2 | §4.5 "for all messages other than PutPartialData, the bits of mask for all active byte lanes must be HIGH"; §4.5 "for data-less operations which are larger than the data bus, all bits of the mask should be HIGH"; §7.2.1, §7.2.2, §8.2.x, §9.3.1 to §9.3.4 (contiguous, naturally aligned) | On A and B, for every message except PutPartialData, mask & active == active. This is the contiguity and natural-alignment requirement of Get, PutFullData, Atomics, Intent, Acquire and Probe, and the all-HIGH requirement of dataless messages larger than the bus (Figure 12) and of every beat of a PutFullData burst | mask, size, address, w | offer | `mask <hex> LOW on active lane(s) <hex> (<opcode> size <s>, address <hex>)` | UL |
| M-MASK-3 | §7.2.3 "any HIGH bits of a_mask must be contained within an aligned region of a_size, but the HIGH bits do not have to be contiguous"; §9.5.3 | PutPartialData on A or B: mask & ~active == 0; any subset of the active lanes, including none and non-contiguous, is legal | mask, size, address, w | offer | `PutPartialData mask <hex> outside its aligned region (size <s>, address <hex>)` | UL |

## 6. M-RSV (reserved fields)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-RSV-1 | §7.2.1, §7.2.4, §8.2.3, §8.2.4, §9.3.1 to §9.3.5, §9.3.7, §9.3.10, §9.3.12, §9.5.1, §9.5.4, §9.5.8, §9.5.9 ("corrupt is reserved and must be 0") | corrupt is LOW on every dataless message: A Get, Intent, AcquireBlock, AcquirePerm; B Get, Intent, ProbeBlock, ProbePerm; C AccessAck, HintAck, ProbeAck, Release; D AccessAck, HintAck, Grant, ReleaseAck. (Table 41 omits the c_corrupt row for Release; the text says reserved, plan §1.5) | corrupt, has_data | offer | `corrupt HIGH on dataless <opcode>` | UL |
| M-RSV-2 | §9.3.12 Table 43 "d_denied is reserved and must be 0" | ReleaseAck carries denied LOW | d_denied | offer | `ReleaseAck denied HIGH (reserved)` | C |

Not checked, by the spec's own words: `data` of dataless messages ("ignored; can be any value"), `sink`
of D responses that are not Grants ("ignored"), `source` of C responses to B requests ("may use any
b_source associated with the sender", §6.4), `d_sink` of ReleaseAck.

## 7. M-ERR (§4.4, §7.2.5, §9.3.7, §9.3.8)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-ERR-1 | §4.4 "denied is only present on the D channel" | The simulator's `denied` field is LOW on every A, B, C and E message (there is no such wire; a model agent that sets it is wrong) | `Msg::denied`, channel | offer | `denied set on <ch> (D-only field)` | UL |
| M-ERR-2 | §4.4 "when a response message that carries data is denied, it must mark all beats of the message as corrupt"; §7.2.5, §9.3.8 | AccessAckData and GrantData with denied HIGH carry corrupt HIGH on every beat | d_denied, d_corrupt per beat | offer of every beat | `denied <opcode> beat <k> with corrupt LOW` | UL |
| M-ERR-3 | §4.4 "no effect on the permissions"; §9.3.7, §9.3.8 "d_param should be ignored, meaning the coherence policy permissions of the block remain unchanged" | A denied Grant or GrantData changes no permission: the monitor leaves `perm[block]` unchanged (the M-PERM exemption). Nothing is observable on the Grant itself (its param is already constrained by M-PARAM-9); a master that behaves as if the denied Grant succeeded is caught later by M-PERM-1 or M-PERM-2 (its next Acquire, Release or ProbeAck starts from the wrong permission). The GrantAck is still due (M-RESP-5 keeps the sink in `grants`) | d_denied, `perm` | last beat accept (state rule) | none of its own; see M-PERM-1/2 | C |

## 8. M-BURST (§4 "forbidden to interleave", §4.1 in-progress rules, §4.2, §8.3)

Lock semantics: on channel ch, when the first beat of a message with beats > 1 is accepted,
`lock[ch]` is set to that message; it is cleared when its last beat is accepted. A single-beat message
takes no lock. Before its first beat is accepted a message may be retracted or replaced freely (§4.1,
Figure 6 message H), so nothing is checked on a first beat except the static rules above.

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-BURST-1 | §4 "the duration of a burst is determined by the channel's size field"; §4.1 "only accepted beats cause the burst to make progress"; §4.4 "all beats of the message must be sent" | The message's beat count is beats(ch, opcode, size), and the beat index offered while the lock is held equals the lock's count of accepted beats (a rejected beat is re-offered with the same index; the index never skips or repeats) | `Msg` beat total and offered beat index (simulator fields), `lock[ch]` | offer | `beat index <k> offered, expected <n> (<opcode> size <s>, <total> beats)` | UH |
| M-BURST-2 | §4.1 "control signals identical to those of the first beat"; §8.3 | While the lock is held and valid is HIGH, the control fields equal the first beat's: A/B: opcode, param, size, source, address; C: opcode, param, size, source, address; D: opcode, param, size, source, sink, denied. Only data, mask and corrupt may change | offered control fields vs `lock[ch]` | offer | `control field <name> changed within burst: <old> -> <new>` | UH |
| M-BURST-3 | §4 "once a burst has begun, the sender must not send beats for any other message until the last beat of the burst has been accepted"; §4.1 "only a beat from the same message burst" | While the lock is held and valid is HIGH, the offered beat belongs to the locked message (same handle) | offered handle vs `lock[ch]` | offer | `beat of <other msg> offered while <locked msg> in progress (beat <n> of <total>)` | UH |
| M-BURST-4 | §4.1 "the master has no choice but to repeat F-1 until it is accepted"; §5 rule 2 | A message in progress is completed: at end of run, and at any reset, no channel holds a lock. (The time-bounded form, "no further beat accepted within `progress_bound`", is the watchdog's rule 2, plan §3.6) | `lock[ch]` | end of run, reset | `burst <opcode> src <o> in progress at end of run: <n> of <total> beats accepted` | UH |
| M-BURST-5 | §4.1 Figure 7 "Message N must eventually be offered again for transaction completion reasons" | A response beat (D or C response opcodes) that was offered, rejected and then retracted (valid LOW or different message on the next cycle) is eventually accepted: at end of run `retracted` is empty. Retracting a response is legal; never re-offering it is not. (Disjoint from M-RESP-7: that rule covers requests whose response was never offered) | `last_offer[ch]`, `retracted` | end of run | `response <opcode> src <o> retracted at cycle <c> and never accepted` | UL |

Legal and therefore not reported: lowering valid during a burst (Figure 6, cycle 5); changing the data
of a rejected in-progress beat (§4.1); rejecting then replacing a first beat (Figure 6, message H).

## 9. M-RESP (Tables 12, 13, 14, 30; §4.2; §6.4; §7.2; §9.3; §9.5; plan §1.4)

Matching, per plan §1.4: a D beat is matched to `outA[d_source]` when d_opcode is AccessAck, AccessAckData,
HintAck, Grant or GrantData, and to `outC[d_source]` when it is ReleaseAck; a C response beat (AccessAck,
AccessAckData, HintAck, ProbeAck, ProbeAckData) is matched to `outB[(*, c_address)]` by address (if several
B requests share the address, the oldest whose opcode admits the response; `c_source` is not used,
§6.4); an E beat is matched to `grants[e_sink]`. Matching happens on the first beat; later beats of a
burst response follow the lock.

Allowed response types (Tables 12, 13, 30):

| request | allowed responses |
|---|---|
| A/B PutFullData, PutPartialData | AccessAck |
| A/B Get, ArithmeticData, LogicalData | AccessAckData |
| A/B Intent | HintAck |
| A AcquireBlock | Grant, GrantData |
| A AcquirePerm | Grant |
| B ProbeBlock | ProbeAck, ProbeAckData |
| B ProbePerm | ProbeAck |
| C Release, ReleaseData | ReleaseAck |
| D Grant, GrantData | E GrantAck |

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-RESP-1 | Tables 12, 13, 30 "the only allowed responses" | The D opcode is an allowed response to the matched request (table above; in particular GrantData never answers AcquirePerm) | d_opcode, `outA`/`outC` entry | first beat accept | `<d_opcode> is not a legal response to <req opcode> (src <o>)` | UL |
| M-RESP-2 | §6.4 "every unanswered message can be uniquely identified"; §4.2 "the first beat of the response ... on the same cycle that the first beat of the request is valid, but not before"; §5.1 "it is illegal to receive a response without receiving the request" | A D beat matches an outstanding request in its namespace: `outA[d_source]` or `outC[d_source]` exists at the time of the D first beat. The monitor processes the same cycle's A and C acceptances before D, so a same-cycle response (§4.2, never produced by this simulator, plan §3.2) is matched; a response one cycle earlier than its request is not, and fires this rule. This is the "not before" bound of §4.2 | d_opcode, d_source, `outA`, `outC` | first beat accept | `unsolicited <d_opcode> src <o>: no outstanding <A|C> request (response before its request, or never requested)` | UL |
| M-RESP-3 | Table 14, §9.5.4 "c_address must match the b_address from the request" | A C response beat (AccessAck, AccessAckData, HintAck, ProbeAck, ProbeAckData) matches an outstanding B request at c_address | c_opcode, c_address, `outB` | first beat accept | `unsolicited <c_opcode> addr <hex>: no outstanding B request` | C |
| M-RESP-4 | Tables 12, 13, 30 | The C response opcode is allowed for the matched B request (ProbeAckData never answers ProbePerm) | c_opcode, `outB` entry | first beat accept | `<c_opcode> is not a legal response to <b_opcode> (addr <hex>)` | C |
| M-RESP-5 | §9.3.9 Table 40 "e_sink ... copied from d_sink"; §6.4 | An E GrantAck matches an unanswered Grant: `grants[e_sink]` exists | e_sink, `grants` | accept | `GrantAck sink <i> with no unanswered Grant` | C |
| M-RESP-6 | §7.2.4, §7.2.5, §8.2.4 "d_size and a_size must always correspond"; §9.3.7, §9.3.8 "must be identical to the original a_size"; §9.3.12 "copied from c_size"; §9.3.5, §9.3.6 "copied from b_size"; §9.5.4, §9.5.5, §9.5.9 | The response's size equals the request's size: d_size == outA/outC size; c_size == outB size | size of response vs request entry | first beat accept | `<resp opcode> size <s> != request size <r>` | UL |
| M-RESP-7 | §5 rule 3ii, §6.2 "receipt of request messages must result in the eventual receipt of a response message" | At end of run every accepted request has received every beat of its response (`outA`, `outB`, `outC`, `grants` empty), excluding requests whose response was offered and retracted (those are M-BURST-5). The time-bounded form is the watchdog's rule 3ii | `outA`, `outB`, `outC`, `grants` | end of run | `request <opcode> src <o> addr <hex> accepted at cycle <c> never answered` | UL |

The copied fields d_source, c_address and e_sink are the matching keys, so "the response copies the
identifier" is M-RESP-2/3/5 rather than separate rules. "All beats of a response are sent" is the lock
(M-BURST-1, M-BURST-4).

## 10. M-ID (§6.4, Table 14; plan §1.4)

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-ID-1 | §6.4 "that value cannot be reused within each channel while the request on that channel is unanswered" | The source of an A request is not outstanding in `outA` when its first beat is accepted (outstanding through the cycle of the response's last beat, plan §0.3) | a_source, `outA` | first beat accept | `A source <o> reused while outstanding (<req opcode> accepted at cycle <c>)` | UL |
| M-ID-2 | §6.4 "separate namespaces" for A and C sources | The source of a C request (Release, ReleaseData) is not outstanding in `outC`; it may equal an outstanding A source | c_source, `outC` | first beat accept | `C source <o> reused while outstanding` | C |
| M-ID-3 | §6.4 "the combination of b_source and b_address be unique" | The (source, address) pair of a B request is not outstanding in `outB` | b_source, b_address, `outB` | first beat accept | `B (source <o>, address <hex>) reused while outstanding` | C |
| M-ID-4 | §6.4 "Channel D must provide unique d_sink transaction identifiers for unanswered Grant requests" | The sink of a Grant or GrantData is not in `grants` (unanswered until its GrantAck is accepted) | d_sink, `grants` | first beat accept | `D sink <i> reused while its GrantAck is pending` | C |

Field widths are M-ALIGN-6/7. The capacity consequence (2^o outstanding per namespace, 2^i unanswered
Grants, plan §1.4) is what these rules enforce; TL-ID checks the pools block the sender instead.

## 11. M-CONC (§9.2 concurrency limits, per block; plan §1.4)

All rules are per block of `block_set(address, size)` and per link (one master per link, so "per
master and block" is "per block"). "Pending" is membership of a `pend[block]` set, from the request's
first beat through the response's last beat (plan §0.3). Checked when the first beat of the new
message is accepted (§9.2 "should not issue"; a rejected and retracted offer was never issued).

Master side (`pend.acq`, `pend.acc`, `pend.rel` are the master's; `pend.probe`, `pend.gack` the slave's):

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-CONC-1 | §9.2 Acquire "should not issue an Acquire if there is a pending Grant, GrantData"; plan §1.4 "one Acquire per block" (strict reading, plan §8) | An Acquire (AcquireBlock, AcquirePerm) is not accepted on a block with `pend.acq` non-empty. With `allow_acquire_under_acquire=1` the rule is skipped (the §9.2 relaxation then only requires a distinct source, which M-ID-1 enforces anyway) | a_opcode, `pend.acq` | first beat accept | `Acquire on block <hex> while Acquire src <o> pending (Grant outstanding)` | C |
| M-CONC-2 | §9.2 Acquire "... AccessAck, or AccessAckData on the block" | An Acquire is not accepted on a block with `pend.acc` non-empty (an uncached Get, Put, Atomic or Intent of this master outstanding on the block) | a_opcode, `pend.acc` | first beat accept | `Acquire on block <hex> while <access opcode> src <o> pending` | C |
| M-CONC-3 | §9.2 Acquire "once the Acquire is issued the master should not issue an Acquire, Get, Put, ArithmeticData, or LogicalData on that block until it receives a Grant or GrantData" | A Get, PutFullData, PutPartialData, ArithmeticData or LogicalData is not accepted on a block with `pend.acq` non-empty (the Acquire half of this sentence is M-CONC-1) | a_opcode, `pend.acq` | first beat accept | `<opcode> on block <hex> while Acquire src <o> pending` | C |
| M-CONC-4 | §9.2 Release "should not issue a Release if there is a pending Grant, GrantData, AccessAck, or AccessAckData on the block" | A Release or ReleaseData is not accepted on a block with `pend.acq` or `pend.acc` non-empty | c_opcode, `pend.acq`, `pend.acc` | first beat accept | `Release on block <hex> while <opcode> src <o> pending` | C |
| M-CONC-5 | §9.2 Release "once the Release is issued, the master should not issue a ProbeAck, ProbeAckData ... on the block until it receives a ReleaseAck" | A ProbeAck or ProbeAckData is not accepted on a block with `pend.rel` non-empty (the Probe itself may arrive and must be answered after the ReleaseAck, Figure 30) | c_opcode, `pend.rel` | first beat accept | `ProbeAck on block <hex> while Release src <o> pending (ReleaseAck outstanding)` | C |
| M-CONC-6 | §9.2 Release "... Acquire, Get, Put, ArithmeticData, LogicalData, or further Release on the block until it receives a ReleaseAck" | An Acquire, Get, Put, Atomic, Release or ReleaseData is not accepted on a block with `pend.rel` non-empty | a/c_opcode, `pend.rel` | first beat accept | `<opcode> on block <hex> while Release src <o> pending` | C |

Slave side:

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-CONC-7 | §9.2 Grant "should not issue a Grant or GrantData if there is a pending ProbeAck or ProbeAckData on the block"; §9.2 Figure 30 "the Grant itself cannot be sent until the expected number of ProbeAcks have been collected" | A Grant or GrantData is not accepted on a block with `pend.probe` non-empty | d_opcode, `pend.probe` | first beat accept | `Grant on block <hex> while Probe src <o> pending` | C |
| M-CONC-8 | §9.2 Grant "once the Grant or GrantData is issued, the slave should not issue Probes on that block until it receives a GrantAck"; §9.2 Probe "should not issue a Probe if there is a pending GrantAck on the block" | A ProbeBlock or ProbePerm is not accepted on a block with `pend.gack` non-empty | b_opcode, `pend.gack` | first beat accept | `Probe on block <hex> while Grant sink <i> awaits GrantAck` | C |
| M-CONC-9 | §9.2 Probe "once the Probe is issued, the slave should not issue further Probes on that block until it receives a ProbeAck or ProbeAckData"; plan §1.4 "at most one Probe outstanding per master and block" | A ProbeBlock or ProbePerm is not accepted on a block with `pend.probe` non-empty | b_opcode, `pend.probe` | first beat accept | `second Probe on block <hex> while Probe src <o> pending` | C |
| M-CONC-10 | §9.2 AccessAck "a slave should not issue an AccessAckData if there is a pending ProbeAckData on the block. AccessAck response without data is allowed, and pending Probe without data is allowed" | An AccessAckData is not accepted on a block with a ProbeBlock in `pend.probe` (a ProbePerm can only be answered by ProbeAck and does not block; an AccessAck or HintAck never blocks) | d_opcode, `pend.probe` (opcode) | first beat accept | `AccessAckData on block <hex> while ProbeBlock src <o> pending` | C |
| M-CONC-11 | §9.2 ProbeAckData "a master should only issue a ProbeAckData.TtoT if C channel messages are kept in order in the network for a given cacheline address" | ProbeAckData with param TtoT is accepted only on a link with `c_ordered=1` | c_opcode, c_param, `c_ordered` | first beat accept | `ProbeAckData TtoT on a link whose C channel is not ordered per address` | C |

Covered elsewhere or by the watchdog, not by M-CONC: "masters must continue to process and respond
to Probes even with an outstanding Grant pending" (§9.2, progress: watchdog 3ii on the Probe); "the
slave must accept the voluntary Release as well as any overtaken ProbeAcks" (Figure 30, progress:
watchdog 3i); "a master with a write miss under a read miss ... must use a unique transaction ID"
(§9.2, M-ID-1, and only reachable with `allow_acquire_under_acquire=1`); "must send a GrantAck for the
first Grant[Data] it receives without waiting for the second" (§9.2, watchdog 3ii on the Grant).

## 12. M-PERM (§9.1.4 Table 31, §9.3, §4.4; plan §8 decision 7)

The monitor tracks `perm[block]` of the link's master, N at reset. Transitions, applied at the last beat
of the message that carries them:

| message accepted | `perm[block]` becomes |
|---|---|
| Grant / GrantData, denied LOW, param toB | B |
| Grant / GrantData, denied LOW, param toT | T |
| Grant / GrantData, denied HIGH | unchanged (the exemption, §9.3.7, §9.3.8, M-ERR-3) |
| ProbeAck / ProbeAckData / Release / ReleaseData, param XtoY | Y (TtoB -> B, TtoN -> N, BtoN -> N, TtoT -> T, BtoB -> B, NtoN -> N) |
| anything else (Probe, GrantAck, ReleaseAck, uncached accesses) | unchanged |

Uncached A accesses by a master that also caches the block are not constrained by the spec beyond
M-CONC (§9.4's implied caps describe the requester's own implied permission, plan §8 decision 8); the
monitor does not relate `perm` to them.

| id | spec § | rule | observation | when | message | level |
|---|---|---|---|---|---|---|
| M-PERM-1 | §9.1.4 Grow "notes both the previous permissions and the new"; §9.3.1, §9.3.2 | An Acquire's Grow param starts from the tracked permission: NtoB and NtoT require perm N; BtoT requires perm B; no Acquire is legal from T | a_param, `perm[block]` | first beat accept | `Acquire <grow> from tracked permission <P> on block <hex>` | C |
| M-PERM-2 | §9.1.4 Prune/Report "notes both the previous permissions and the new, lesser permissions"; §9.3.5, §9.3.6, §9.3.10, §9.3.11 | A ProbeAck, ProbeAckData, Release or ReleaseData param starts from the tracked permission: TtoB, TtoN, TtoT require T; BtoN, BtoB require B; NtoN requires N (and is legal only on ProbeAck, M-PARAM-8). A master that treated a denied Grant as a success fires this rule here | c_param, `perm[block]` | first beat accept | `<opcode> <prune> from tracked permission <P> on block <hex>` | C |
| M-PERM-3 | §9.3.3, §9.3.4 Cap "what they should become"; §4.4 "master agents that cache blocks of data must always restore permissions to their slave upon request" | A ProbeAck or ProbeAckData answering a Probe with cap toN ends at N (TtoN, BtoN, NtoN); cap toB ends at N or B (TtoB, TtoN, BtoN, BtoB, NtoN); cap toT accepts any (TtoT included, subject to M-CONC-11). "Probing to cap at a more permissive level than they currently have is allowed, and does not result in a permissions change" (§9.3.3), so a Report from a permission already within the cap is legal | c_param, `outB` entry's param | first beat accept | `<opcode> <prune> does not satisfy Probe cap <cap> on block <hex>` | C |

The fourth check the plan lists under M-PERM, "the Grant cap is at least the requested permission", is
M-PARAM-11 (it needs only the outstanding Acquire's param); it is not duplicated here.

## 13. What the monitor cannot check (plan §3.6)

| rule | why the monitor cannot observe it | how it is covered |
|---|---|---|
| §4.1 "valid must never depend on ready" (same cycle), "no combinational path from ready to valid or the control and data signals" | the monitor sees beats, not dependencies | by construction of the cycle (plan §3.2): valid and the beat are set in phase 1 from the state at the start of the cycle, ready is computed in phase 2; no fixed point exists for one to depend on the other |
| §4.1 "a low priority valid may not combinationally depend on a high priority valid" | same | same: every valid is computed in phase 1 from the previous cycle's state; an agent cannot see another channel's current valid when it sets its own |
| §4.1 "a high priority ready may not combinationally depend on a low priority ready" | same | same: a ready of phase 2 may look at the offer of phase 1 on its own link, never at another ready (plan §3.2); the `Xbar` accepts C, D and E independently of A and B occupancy (plan §3.5) |
| §4.2 "ready high on the response channel implies ready high on the request channel" in the same-cycle case | there is no same-cycle response (plan §8 decision 2) | not applicable; M-RESP-2 still checks the "not before" bound |
| §5 rule 1, no cycle in the agent graph | a structural property | `GraphCheck` at elaboration (T4) |
| §5 rule 2 (present all beats of a received message), rule 3i (eventually accept a presented beat), rule 3ii (eventually answer a received request), §4.3 "timeouts within the TileLink network are forbidden" | "eventually" has no finite observation; a finite timer cannot distinguish an illegal permanent stall from a legal long delay | the watchdog (T4) with the declared `progress_bound`: a trip is a diagnostic naming the rule (2, 3i, 3ii), the link, the message and the bound, not a conformance verdict; M-BURST-4, M-BURST-5 and M-RESP-7 state the end-of-run forms |
| §5.1 rule 3's exemption ("unless they have a higher priority message in flight or unanswered") | the watchdog does not model which stall is excused | the dump prints every timer past the bound so the reader can apply the exemption |
| §6.5 operation ordering (a response only once the effect is visible; serialization of Puts) | not a per-link wire property | `ValueCheck` (plan §3.6) under `stamp` or `full` data |
| §9 single-writer/multiple-reader across masters | spans several links | `CoherenceCheck` (T6) |
| §6.3 side effects, cacheability, FIFO domains | properties of the address map, not of beats | configuration |
| plan §5.2 admission contract, plan §5.3 resource argument | elaboration-time arguments | `checkProtocols` and TL-OCT-EVICT |

"0 monitor violations" therefore means no observed violation of rules M-RESET-1 to M-PERM-3; conformance
rests on the three legs of plan §3.6.

## 14. Decisions recorded here (plan §8 and this file)

| topic | decision | where |
|---|---|---|
| Grant toN | never encoded, denied or not (rocket-chip's rule, stricter than §9.3.7/9.3.8) | M-PARAM-9, plan §8.7 |
| successful Grant cap | meets or exceeds the Acquire's Grow | M-PARAM-11, plan §8.7 |
| denied Grant | permission unchanged, param ignored, GrantAck still due | M-ERR-3, M-PERM table, M-RESP-5 |
| one Acquire per block | strict reading of §9.2; `allow_acquire_under_acquire` for experiments | M-CONC-1, plan §1.4 |
| response matching | D by source and opcode namespace (A vs C requests); C by address; E by sink | M-RESP-2/3/5, plan §1.4 |
| "outstanding" and "pending" | through the response's last beat, inclusive (not §6.4's first beat) | §0.3 |
| checks of static field rules | on offer (valid HIGH), once per beat | §0.2 |
| checks of matching, ID, concurrency and permission rules | on the first beat's acceptance ("issued" = received) | §0.2 |
| B-channel Access/Hint messages | legal on TL-C links, matched and checked like Probes; no agent emits them | M-LEVEL-3, M-RESP-3/4, plan §2 |
| ProbeAckData TtoT | legal only with `c_ordered=1` | M-CONC-11 |
| retracted response versus unanswered request | M-BURST-5 when the response was offered and retracted; M-RESP-7 when it was never offered | §8, §9 |
| Figure 10 third transaction | drawn a_opcode 1 (PutPartialData) answered by d_opcode 1 (AccessAckData), which M-RESP-1 forbids; the golden uses the text's intent (an Atomic) | `TileLink/test/golden/fig10.txt` |
