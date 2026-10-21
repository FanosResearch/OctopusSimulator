# TL-WAVE scenarios: how each golden transcript is regenerated

Companion of `fig06.txt` to `fig22.txt` in this directory (plan §6 TL-WAVE, steps T2 and T3). Each
scenario drives a scripted master and a scripted slave (or a `RamSlave` where the figure's timing is the
simulator's minimum) on one link `L0` so that the accepted-beat transcript equals the golden, beat for
beat. Written 2026-10-21 from the PDF waveforms; the spec's page offset is 0 (printed page = PDF page).

## 1. Harness

### 1.1 Scripted agents (test-only, `TileLink/test/`)

| agent | behaviour |
|---|---|
| `ScriptedMaster` | a list of A-channel offers `(cycle, message, beat)`: in that cycle it drives `a_valid` HIGH with that beat; in every other cycle `a_valid` is LOW. It never retries on its own: a rejected beat appears again in the list if the figure re-offers it. `d_ready` follows a per-cycle list (default HIGH). TL-C variants add C and E offers and `b_ready` |
| `ScriptedSlave` | `a_ready` per cycle (default HIGH); D-channel offers `(cycle, message, beat)` as above. It does not look at requests; the test author guarantees that every scripted response answers an accepted request (the monitor checks it, M-RESP-2) |
| `RamSlave` | the real slave (plan §3.5) with `latency 1`: the first response beat is offered in the cycle after the request's last accepted beat (the minimum of plan §3.2) and later beats follow on consecutive cycles while `d_ready` is HIGH |

The monitor is attached to `L0` in fatal mode; every scenario must run to its end with 0 violations.
`reset_cycles = 100`; `data_mode = full`.

### 1.2 Cycle alignment and comparison

- `t0` is the run cycle that corresponds to the golden's cycle 0 (the golden's cycle k is run cycle
  `t0 + k`). Unless stated, `t0 = 100` (the first cycle after reset); the golden's cycle 0 never offers
  anything, so the first offer is at run cycle 101.
- The test takes the transcript lines of `L0` with cycle in `[t0, t0 + N)` (N = the golden's last cycle
  plus one), subtracts `t0`, keeps the channels that occur in the golden's compared section (A only for
  Figures 6, 11, 12; D only for Figure 7; A and D elsewhere), and compares them line by line with the
  section named by the golden's `# compare:` line. A golden field written `-` is not compared.
- Sources, addresses, masks and data that the figure does not draw are fixed below so that the run is
  legal under the monitor; they are `-` in the golden and only the monitor checks them.
- `data` of a Put beat is the byte pattern "low byte of the effective address" (as in Figure 11) unless
  the figure draws a value.

Ready/valid schedules below list only the cycles in which a signal differs from its default (`valid`
default LOW, `ready` default HIGH after cycle 0). Cycle numbers are golden cycles.

## 2. Figure 6 (`fig06.txt`): Channel A ready/valid, w = 8

Link: `w=8 a=32 z=3 o=3 i=0 level=UH`, `max_size=6`. Agents: `ScriptedMaster` M, `ScriptedSlave` S.

Messages (all PutFullData except J; the slave never answers inside the window, so S has no D offers;
the six responses arrive after cycle 16 and are outside the compared range):

| name | opcode | size | source | address | mask | beats |
|---|---|---|---|---|---|---|
| F | PutFullData | 5 | 0 | 0x100 | 0xff each beat | 4 |
| G | PutFullData | 0 | 1 | 0x201 | 0x02 | 1 |
| H | PutFullData | 6 | 2 | 0x400 | 0xff | 8 (never sent) |
| I | PutFullData | 2 | 3 | 0x304 | 0xf0 | 1 |
| J | Get | 4 | 4 | 0x500 | 0xff (dataless, 2^size >= w: all HIGH) | 1 |
| K | PutFullData | 1 | 5 | 0x602 | 0x0c | 1 |

M offers (`a_valid` HIGH): c1 F-0, c2 F-0, c4 F-1, c6 F-1, c7 F-2, c8 F-3, c9 G-0, c11 H-0, c12 I-0,
c14 J-0, c15 K-0.
S `a_ready`: LOW at c0, c1, c4, c11; HIGH otherwise.

Accepted: c2 F-0, c6 F-1, c7 F-2, c8 F-3, c9 G, c12 I, c14 J, c15 K. H is rejected at c11 and never
re-offered (legal: its first beat was never accepted). After c16, S answers the five accepted requests
(any order, AccessAck for the Puts, a 2-beat AccessAckData for J) so that the run ends with M-RESP-7
satisfied: schedule them at c20..c26.

## 3. Figure 7 (`fig07.txt`): Channel D ready/valid, w = 8

Link: `w=8 a=32 z=3 o=3 i=0 level=UH`. Agents: `ScriptedMaster` M, `ScriptedSlave` S. `t0 = 110`.

Requests, issued by M before the window (run cycles 100..105, S `a_ready` HIGH, one message per cycle,
the Put burst over two cycles), so that every drawn response answers an outstanding request:

| run cycle | request | size | source | address | answered by |
|---|---|---|---|---|---|
| 100 | Get | 5 | 0 | 0x100 | L (AccessAckData, 4 beats) |
| 101 | Get | 0 | 1 | 0x201 | M (AccessAckData, 1 beat) |
| 102 | Get | 4 | 2 | 0x300 | N (AccessAckData, 2 beats) |
| 103 | PutFullData | 0 | 3 | 0x401 | O (AccessAck) |
| 104, 105 | PutFullData (2 beats) | 4 | 4 | 0x500 | P (AccessAck) |

S D offers (golden cycles): c1 L-0, c2 L-0, c4 L-1, c6 L-1, c7 L-2, c8 L-3, c9 M-0, c11 N-0, c12 O-0,
c14 P-0, c15 N-0, c16 N-1.
M `d_ready`: LOW at c0, c1, c4, c11; HIGH otherwise.

Accepted: c2 L-0, c6 L-1, c7 L-2, c8 L-3, c9 M, c12 O, c14 P, c15 N-0, c16 N-1. N is rejected at c11,
retracted at c12 (S offers O instead, legal), and re-offered at c15 (M-BURST-5). See the golden's
AMBIGUITY note on the figure's "N-0" label at c16.

## 4. Figure 8 (`fig08.txt`): Get and AccessAckData, max and min delay

Link: `w=8 a=32 z=3 o=1 i=0 level=UH`. Agents: `ScriptedMaster` M, `ScriptedSlave` S.

| request | size | source | address | mask |
|---|---|---|---|---|
| Get#1 | 5 | 0 | 0x100 | 0xff |
| Get#2 | 5 | 1 | 0x200 | 0xff |

M offers: c1 Get#1, c9 Get#2 (rejected), c10 Get#2.
S `a_ready`: LOW at c0 and c9. S D offers (the `expected_plus1` timing): AccessAckData src 0 beats at
c4, c5, c6, c7; AccessAckData src 1 beats at c11, c12, c13, c14.

The figure's second response starts at c10, the cycle of the request's acceptance; the simulator
cannot offer it before c11 (plan §3.2), hence `compare: expected_plus1`. Equivalent `RamSlave`
setting: a per-request latency list `3, 1` (cycles from the request's acceptance to the first response
beat) with the c9 ready drop supplied by a `Throttle` scripted to reject c9 only.

## 5. Figure 9 (`fig09.txt`): PutFullData and AccessAck, max and min delay

Link: `w=8 a=32 z=3 o=1 i=0 level=UH`. Agents: `ScriptedMaster` M, `ScriptedSlave` S.

| request | size | source | address | mask | beats |
|---|---|---|---|---|---|
| Put#1 | 5 | 0 | 0x100 | 0xff | 4 |
| Put#2 | 5 | 1 | 0x200 | 0xff | 4 |

M offers: c1..c4 Put#1 beats 0..3; c9..c12 Put#2 beats 0..3.
S `a_ready`: always HIGH from c1. S D offers: AccessAck src 0 at c7; AccessAck src 1 at c10 (drawn c9;
`expected_plus1`). The second AccessAck is accepted while M is still sending beats 1..3 of Put#2: M's
`d_ready` stays HIGH (§4.2.2: the master must accept or buffer a concurrent response).

A `RamSlave` cannot produce the second timing (it acknowledges a Put after its last beat); the
scripted slave is required.

## 6. Figure 10 (`fig10.txt`): ArithmeticData and AccessAckData, both bursts

Link: `w=8 a=32 z=3 o=2 i=0 level=UH`. Agents: `ScriptedMaster` M, `ScriptedSlave` S.

| request | opcode | param | size | source | address | mask | beats |
|---|---|---|---|---|---|---|---|
| Atomic#1 | ArithmeticData | 4 (ADD) | 4 | 0 | 0x100 | 0xff | 2 |
| Atomic#2 | ArithmeticData | 4 (ADD) | 4 | 1 | 0x200 | 0xff | 2 |
| Third | ArithmeticData (drawn PutPartialData; golden's FIGURE ERROR note) | 4 (ADD) | 3 | 2 | 0x308 | 0xff | 1 |

M offers: c1 Atomic#1 beat 0, c2 beat 1; c8 Atomic#2 beat 0, c9 beat 1 (rejected), c10 beat 1; c13 Third.
S `a_ready`: LOW at c0 and c9. S D offers (`expected_plus1`): AccessAckData src 0 beats at c5, c6;
AccessAckData src 1 beat 0 at c9, beat 1 at c11 (one cycle after the request beat it depends on, which
was accepted at c10); AccessAckData src 2 at c14.

The `expected_as_drawn` section has the same-cycle responses (c8, c10, c13) and is documentation only.

## 7. Figure 11 (`fig11.txt`): byte lanes on a 16-byte bus

Link: `w=16 a=32 z=3 o=3 i=0 level=UH`, `max_size=6`. Agents: `ScriptedMaster` M, `RamSlave` S
(`latency 1`, ready always HIGH). Only channel A is compared.

M offers PutFullData with sources 0..5 in order, exactly as the golden's waveform table: c1 and c2
(size 5, 0x40, beats 0 and 1), c4 (size 4, 0x10), c6 (size 3, 0x58), c8 (size 2, 0x24), c10 (size 1,
0x76), c12 (size 0, 0xc1). Masks and data as in the golden (mask derived by §4.5; data byte = low byte
of the lane's address). S's AccessAcks (c4, c6, c8, c10, c12, c14) are on D and not compared.

## 8. Figure 12 (`fig12.txt`): mask bits on a 4-byte bus

Link: `w=4 a=32 z=2 o=3 i=0 level=UH`, `max_size=3`. Agents: `ScriptedMaster` M, `RamSlave` S
(`latency 1`). Only channel A is compared.

M offers, sources 0..5 in order: c1, c2 PutFullData size 3 at 0x40 (mask 0xf, 0xf); c4, c5
PutPartialData size 3 at 0x40 (mask 0xa, 0x4); c7 Get size 3 at 0x40 (mask 0xf); c9 PutFullData size 1
at 0x62 (mask 0xc); c11 PutPartialData size 1 at 0x62 (mask 0x4); c13 Get size 1 at 0x62 (mask 0xc).
Data: any bytes (not compared).

## 9. Figure 17 (`fig17.txt`): responses out of order

Link: `w=8 a=32 z=3 o=2 i=0 level=UH`. Agents: `ScriptedMaster` M, `ScriptedSlave` S.

| golden cycle | request | size | source | address | mask | param |
|---|---|---|---|---|---|---|
| c1 | PutFullData | 3 | 1 | 0x100 | 0xff | 0 |
| c2 | Get | 3 | 2 | 0x108 | 0xff | 0 |
| c3 | Intent | 3 | 3 | 0x110 | 0xff | 0 (PrefetchRead; not drawn) |

S D offers: c6 HintAck src 3 size 3; c7 AccessAck src 1 size 3; c8 AccessAckData src 2 size 3 (1 beat).
All ready signals HIGH from c1. The order 3, 1, 2 is the point of the figure; M-RESP-2 matches each by
source.

## 10. Figure 18 (`fig18.txt`): TL-UL Get and Put

Link: `w=4 a=32 z=2 o=0 i=0 level=UL`, `max_size=2`. Agents: `ScriptedMaster` M, `RamSlave` S
(`latency 1`, `full` data, memory zeroed at reset). The real slave reproduces the figure's one-cycle
responses exactly.

M offers, all at address 0x0, size 2, source 0 (one request outstanding at a time, so o = 0 suffices):
c1 PutFullData mask 0xf data 0x000000ab; c5 Get mask 0xf; c9 PutFullData mask 0xf data 0x0;
c13 PutPartialData mask 0x3 data 0x0000000f; c17 Get mask 0xf.

S answers at c2, c6, c10, c14, c18. The AccessAckData at c6 carries 0xab; the one at c18 carries 0xf
(the golden's DATA NOTE: the figure draws 0x3, which no memory can return here).

## 11. Figure 22 (`fig22.txt`): TL-UH Atomic and Hint

Link: `w=8 a=32 z=3 o=0 i=0 level=UH`, `max_size=3`. Agents: `ScriptedMaster` M, `RamSlave` S
(`latency 1`, `full` data, memory zeroed, atomics computed).

M offers, all at address 0x0, size 3, source 0, mask 0xff: c1 Intent param 1 (PrefetchWrite); c5
PutFullData data 0x1; c9 ArithmeticData param 4 (ADD) data 0x1; c13 LogicalData param 3 (SWAP) data
0x3; c17 Get.

S answers at c2 (HintAck), c6 (AccessAck), c10 (AccessAckData 0x1, memory now 0x2), c14 (AccessAckData
0x2, memory now 0x3), c18 (AccessAckData 0x3). The whole data column is compared.

## 12. Which scenarios belong to which step

| step | scenarios | why |
|---|---|---|
| T2 (TL-UL/UH monitor, `RamSlave`, `ScriptedMaster`/`ScriptedSlave`) | Figures 17, 18 (TL-UL figures and single-beat TL-UH) | no bursts, no atomics |
| T3 (bursts, atomics, `full` data) | Figures 6, 7, 8, 9, 10, 11, 12, 22 | bursts or atomics |

The scripted agents are test fixtures; they live under `TileLink/test/`, not in `src/agents/`.
