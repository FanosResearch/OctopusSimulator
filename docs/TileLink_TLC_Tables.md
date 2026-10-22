# TL-C coherence tables (T5): `TLC_MESI_L1.csv`, `TLC_MESI_LLC.csv`, every cell justified

Status: T5 design deliverable of `docs/TileLink_Plan.md`, written 2026-10-21 on branch `tilelink`. No
code reads these tables yet; T6 (policy engine, `CacheMaster`, `CoherentManager`) and T7 (`TLC_L1`,
`TLC_LLC` handlers) will. Review this document before either is written. The source of truth is the two
CSV files in `TileLink/tables/`; this document is generated from the same description, so the cell
text printed here is byte-for-byte the CSV cell. Section numbers "S9.2" are the TileLink 1.9.3
specification's, "plan S5.1" this repository's `docs/TileLink_Plan.md`, "Primer 8.x" is Sorin, Hill and
Wood, *A Primer on Memory Consistency and Cache Coherence*, chapter 8 (directory protocols).

## 1. Format, conventions and the engine rules the tables assume

### 1.1 The CSV format (as `src/FSMReader.cpp` reads it)

- Four sections, each introduced by a header line and the first three closed by a line whose second
  field is empty: `EventNum,Event`, `ActionNum,Action`, `StateNum,State`, then the table whose header
  is `State,stable,isDataValid,<one column per event in EventNum order>`.
- Transition rows are indexed by **position**: the k-th row after the table header must be the state
  with `StateNum` k (`FSMReader::getTransition` indexes `m_states[current_state]`). Event columns are
  indexed by position too (`event_index++` per field), so the column order is the `EventNum` order.
- A cell is `action/action/.../next_state`. The text after the last `/` is the next state; empty after
  the last `/` means "stay" (`Fault/`, `Hit/`). A cell with no `/` is a bare next state with no action
  (`I`). An empty cell means stay with no action. Whitespace is trimmed; the file must not end with a
  blank line after the last row (an empty line would be parsed as a state named "").
- `stable` is 1 for the stable states; `isDataValid` is the Octopus "line allocated / `cache_line.valid`"
  bit (`FSMReader::isValidState`), not the TileLink permission: every state that owns an MSHR entry or an
  array line is 1, as in `MESI_directory_v2.csv` (IS_d is 1 there). The L1 transient states IS_a and IM_a
  therefore carry 1 although they hold no readable copy; the permission is the table's business.

### 1.2 Engine rules (what the policy engine does around a cell, identically in `tlsim` and Octopus)

1. **Decode.** A received message and the line state give one event (the decode tables in S2.2 and
   S3.2). A message the decoder cannot map is a fault at decode time, as `LLCMSIDirectory::readEvent`
   exits on an "Invalid Transaction" today; the table cells marked `Fault/` are the ones that decode
   to a legal event name but are impossible in that state. Exception (T6): while the block has Probes
   outstanding, an Acquire whose param contradicts the directory (NtoB/NtoT from a listed holder, BtoT
   from the listed Tip) is decoded by its param, not faulted. A and C are unordered, so a master's
   Acquire can overtake its own ProbeAck; every probing state Stalls inner A, and the message is
   re-decoded once the ProbeAck has removed the sender (S3.2's "decode fault from a listed holder"
   holds only with no Probe outstanding).
2. **Stall parks the message.** A cell with `Stall` leaves the message in the controller's queue (it is
   `FRFCFS_State::NonReady`, as in `MSIDirectory::getRequestState`) and re-decodes it when the line's
   state changes. A parked message never withholds `ready` on its channel: the receive queues of plan
   S5.2 keep accepting, so a Stall never violates forward-progress rule 3(i), and the one case where a
   master legitimately refuses to answer (a Probe during an outstanding Release) is covered by rule 3's
   exception (a higher-priority C message of its own is unanswered).
3. **Parking a request.** A cell that leaves a stable state for a transient one on an A-channel or
   CPU-side event parks the consumed request (Octopus: `ADD_PENDING`, as `GetData` does today). The
   cell that later carries `Grant*`, `AccessAckData_in`, `Execute`, `ForwardIn`, `HintAck*` or `Hit`
   answers the parked request and releases it (`REMOVE_PENDING` / `HIT_Action`). Its channel
   reservation is kept from the parking cell to the answering cell (plan S5.2 step 1, "saved-request
   completions keep the reservation they took when parked").
4. **The manager's probe counter.** `ProbeBlock_toN_all` sets the block's counter to the number of
   Probes it sent; `ProbeBlock_toN_owner` and `ProbeBlock_toB_owner` set it to 1; every ProbeAck cell
   carries `DecCount`. The decoder yields the `last_` event when the counter is 1 before the decrement.
   The counter counts Probes sent, never holders: a Release merged into the flow (Figure 30) removes
   a holder but not an expected ProbeAck.
5. **Holder bookkeeping.** `SetOwner` makes the requester the Tip and empties the sharer set;
   `AddSharer` adds the requester to the sharer set; `OwnerToSharer` moves the Tip into the sharer set;
   `RemoveHolder` removes the sender of the consumed C message from whichever set lists it (a no-op for
   a NtoN sender that a Release already removed). "Holders" below means owner plus sharers.
6. **Data.** `SaveData` writes the consumed message's data (fill, ProbeAckData, ReleaseData) into the LLC
   line; `Drop` frees the line without a write; `PutFullData_out` reads the line and writes it outward.
   The L1's `Hit` serves the CPU from the line and, for a Store, writes it (`MODIFY_DATA`). The data array
   is used exactly as Octopus's `CacheDataHandler` uses it: the LLC allocates on an Acquire fill, always
   holds the line while any L1 does (inclusion), and the eviction write-back of a clean line is the data
   handler's business, not the table's.
7. **The eviction's write-back is a port transaction.** `PutFullData_out` on a `Replacement` row (states
   I, BN_e, TN_e) sends the line outward and the block goes to N; the `AccessAck` is consumed by the
   outer port (plan S5.1: `K_MEM_WACK`, hold H1 = later outer requests for that block wait for it). Only
   the writes issued by a CMO row (`I_w`, `B_w`, `N_w`), the Get of `NT_d` and the forwarded access of
   `N_u` raise an outer-response event in the table. This keeps the Octopus data array semantics: the
   victim's slot is reused by the fill while the write sits in the pending-write-back register.
8. **Denied Grant param.** `Grant_denied` encodes `denied = 1` and `param` = the cap the request asked for
   (NtoB: toB; NtoT, BtoT: toT), never toN (plan S8 decision 7); the master ignores it (S9.3.7).

### 1.3 Citation keys used in the justifications

| key | rule |
|---|---|
| S9.2-A | master: no Acquire while a Grant or AccessAck is pending on the block; after an Acquire, no Acquire/Get/Put/Atomic until the Grant |
| S9.2-G | slave: no Grant while a ProbeAck is pending; after a Grant, no Probe until the GrantAck |
| S9.2-AA | slave: no AccessAckData while a ProbeAckData is pending |
| S9.2-R | master: no Release while a Grant or AccessAck is pending; after a Release nothing (ProbeAck included) until the ReleaseAck |
| S9.2-P | slave: no Probe while a GrantAck is pending; at most one Probe outstanding per master and block |
| S9.2-PAD | master: ProbeAckData TtoT only with per-address ordered C |
| Figure 29 | two Acquires on one block; the Grant to a master probed after its Acquire must carry data; GrantAck serializes |
| Figure 30 | voluntary Release merged with an ongoing Probe; the slave accepts the Release and acknowledges it, the master answers the Probe after the ReleaseAck |
| S9.3.x | the message's field rules (param category, data, denied) |
| Table 29 | permissions versus accesses (only a Tip with no Branches may write) |
| S6.5 | a slave responds only once the effect is visible everywhere; a later operation is ordered after the response |
| S8.2.3 | CMOs as Intent; HintAck carries completion |
| D7, D8 | plan S8 decisions 7 (Grant never toN, meets or exceeds) and 8 (uncached accesses: Get probes the owner toB, Put/Atomic probe every holder toN) |
| M-CONC, M-PERM | the monitor rules of plan S3.6 |

Sizes: L1: 10 states x 12 events = 120 cells (120 non-empty, 53 Fault, 23 Stall); LLC: 26 states x 26 events = 676 cells (676 non-empty, 285 Fault, 246 Stall).

## 2. `TileLink/tables/TLC_MESI_L1.csv`: the L1 master

### 2.1 States

| state | stable | TileLink permission | meaning |
|---|---|---|---|
| I | yes | None | no copy |
| IS_a | no | None | AcquireBlock NtoB sent for a Load; waiting for GrantData |
| IM_a | no | None | AcquireBlock NtoT sent for a Store, or a BtoT whose Branch was pruned by a Probe toN meanwhile; waiting for GrantData toT |
| SM_a | no | Branch | AcquireBlock BtoT sent for a Store; the Branch is intact; waiting for Grant or GrantData toT |
| S | yes | Branch | clean read-only copy |
| E | yes | Tip, no Branches | clean copy, writable without a message (policy state on top of Tip, plan S5.1) |
| M | yes | Tip, no Branches | dirty copy |
| SI_r | no | Branch leaving | Release BtoN sent; waiting for ReleaseAck; Probes held |
| EI_r | no | Tip leaving | Release TtoN sent; waiting; Probes held |
| MI_r | no | Tip leaving | ReleaseData TtoN sent; waiting; Probes held |

MSI is this table with E removed: `IS_a + GrantData_toT` goes to M only if the manager never grants toT
on NtoB, so an MSI preset is the same L1 table with an MSI manager that answers NtoB with toB
(the one cell `I + AcquireBlock_NtoB` of the manager); no second L1 file is needed.

### 2.2 Events and their decode

| event | from | decode |
|---|---|---|
| Load, Store | CPU | `complementary_value` 0 / 1, as `MSIDirectory::readEvent` |
| Replacement | self | the controller's victim message (`K_EVICT`) |
| ProbeBlock_toN / _toB / _toT | B | opcode ProbeBlock, `param` = Cap toN (2) / toB (1) / toT (0). ProbePerm is not decoded (never sent by the manager table; a foreign manager's ProbePerm would be answered like ProbeBlock without data only in states that have no dirty data, so it is left a decode fault for now) |
| Grant_toB / Grant_toT | D | opcode Grant, `denied` = 0, `param` toB / toT |
| GrantData_toB / GrantData_toT | D | opcode GrantData, `denied` = 0, `param` toB / toT |
| Grant_denied | D | opcode Grant or GrantData with `denied` = 1 (`param` ignored, S9.3.7) |
| ReleaseAck | D | opcode ReleaseAck |

A Grant[Data] with `param` toN is a decode fault (D7, M-PARAM). An unknown D opcode (AccessAck,
AccessAckData, HintAck) is a decode fault: the L1 issues no uncached access.

### 2.3 Actions and what they emit

| action | emits | channel | param | data |
|---|---|---|---|---|
| AcquireBlock_NtoB / _NtoT / _BtoT | AcquireBlock | A | Grow NtoB (0) / NtoT (1) / BtoT (2) | no |
| GrantAck | GrantAck | E | (sink copied) | no |
| ProbeAck_TtoB / _TtoN / _BtoN | ProbeAck | C | Prune TtoB (0) / TtoN (1) / BtoN (2) | no |
| ProbeAck_TtoT / _BtoB / _NtoN | ProbeAck | C | Report TtoT (3) / BtoB (4) / NtoN (5) | no |
| ProbeAckData_TtoB / _TtoN | ProbeAckData | C | Prune TtoB / TtoN | the line |
| Release_TtoN / _BtoN | Release | C | Prune TtoN / BtoN | no |
| ReleaseData_TtoN | ReleaseData | C | Prune TtoN | the line |
| Hit | the CPU response | (CPU) | | |
| Error | the CPU response, marked as an access error (the denied Grant) | (CPU) | | |
| Stall, Fault | nothing | | | |

### 2.4 Every cell

Format: `(state, event) -> cell`: justification. `cell` is the CSV text.


#### Row I

- (I, Load) -> `AcquireBlock_NtoB/IS_a`: AcquireBlock NtoB [Grow, S9.3.1], park the Load, wait for the Grant. M-CONC: no Grant pending on the block in I, so the Acquire is allowed [S9.2-A]. Primer 8.2: I + load -> GetS.
- (I, Store) -> `AcquireBlock_NtoT/IM_a`: AcquireBlock NtoT [Grow]: a Store needs Tip-with-no-Branches [Table 29]. Primer 8.2: I + store -> GetM.
- (I, Replacement) -> `Fault/`: Fault: an invalid line is never a victim (CacheController::checkReplacements evicts valid lines only); nothing to Release and Release NtoN is illegal [S9.3.10].
- (I, ProbeBlock_toN) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (I, ProbeBlock_toB) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (I, ProbeBlock_toT) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (I, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (I, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (I, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (I, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (I, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (I, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row IS_a

- (IS_a, Load) -> `Stall/`: Stall: one Acquire per block (plan S1.4, strict reading of [S9.2-A]); the access waits for the Grant.
- (IS_a, Store) -> `Stall/`: Stall: one Acquire per block (plan S1.4, strict reading of [S9.2-A]); the access waits for the Grant.
- (IS_a, Replacement) -> `Stall/`: Stall: a Release may not be issued while a Grant is pending [S9.2-R]; the eviction waits for the Grant (as SM_ad/IS_d + Replacement = Stall in MESI_directory_v2).
- (IS_a, ProbeBlock_toN) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IS_a, ProbeBlock_toB) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IS_a, ProbeBlock_toT) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IS_a, Grant_toB) -> `Fault/`: Fault: the master has no copy, so a Grant without data cannot complete an AcquireBlock [S9.3.1: AcquireBlock obtains a copy]; the manager must send GrantData [S9.2 Figure 29 step 8].
- (IS_a, Grant_toT) -> `Fault/`: Fault: the master has no copy, so a Grant without data cannot complete an AcquireBlock [S9.3.1: AcquireBlock obtains a copy]; the manager must send GrantData [S9.2 Figure 29 step 8].
- (IS_a, GrantData_toB) -> `Hit/GrantAck/S`: GrantData toB meets NtoB [D7]: the copy is a Branch; serve the parked Load (Hit), send GrantAck [S9.3.9, E channel], S. Primer 8.2: IS_d + Data -> S.
- (IS_a, GrantData_toT) -> `Hit/GrantAck/E`: GrantData toT exceeds NtoB, which S9.3.7-8 allow and D7 permits (no other copy existed, plan S5.1): the copy is a clean Tip with no Branches = E. Hit, GrantAck. Primer 8.5 (MESI): exclusive data on a read miss -> E.
- (IS_a, Grant_denied) -> `Error/GrantAck/I`: The Acquire had no effect: permissions unchanged = None [S9.3.7 d_denied]. The parked Load completes with an access error (Error), no line allocated; the GrantAck is still owed because a Grant[Data] was received [S9.3.9; TL-DENY]. M-PERM exempts the denied param.
- (IS_a, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row IM_a

- (IM_a, Load) -> `Stall/`: Stall: one Acquire per block (plan S1.4, strict reading of [S9.2-A]); the access waits for the Grant. IM_a holds no copy (reached from I, or from SM_a after a Probe toN), so a Load cannot hit.
- (IM_a, Store) -> `Stall/`: Stall: one Acquire per block (plan S1.4, strict reading of [S9.2-A]); the access waits for the Grant.
- (IM_a, Replacement) -> `Stall/`: Stall: a Release may not be issued while a Grant is pending [S9.2-R]; the eviction waits for the Grant (as SM_ad/IS_d + Replacement = Stall in MESI_directory_v2).
- (IM_a, ProbeBlock_toN) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IM_a, ProbeBlock_toB) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IM_a, ProbeBlock_toT) -> `ProbeAck_NtoN/`: ProbeAck_NtoN, stay: the master holds no copy, so it reports None [S9.3.5 Report]. Legal and required: a Probe must be answered even with a Grant pending [S9.2 Figure 29 step 3]; the Probe was sent before the manager processed this master's Acquire (or before it processed its Release, Figure 30), so it does not concern the copy being acquired.
- (IM_a, Grant_toB) -> `Fault/`: Fault: a successful Grant must meet the request; NtoT (or BtoT) asks for Tip, toB does not meet it [D7, M-PARAM].
- (IM_a, Grant_toT) -> `Fault/`: Fault: the master has no copy, so a Grant without data cannot complete an AcquireBlock [S9.3.1: AcquireBlock obtains a copy]; the manager must send GrantData [S9.2 Figure 29 step 8]. For a BtoT requester that reached IM_a through a Probe toN this is exactly the Figure 29 step 8 obligation on the manager.
- (IM_a, GrantData_toB) -> `Fault/`: Fault: toB does not meet NtoT/BtoT [D7, M-PARAM].
- (IM_a, GrantData_toT) -> `Hit/GrantAck/M`: GrantData toT meets NtoT/BtoT [D7]: Tip with no Branches, the parked Store is performed (Hit) so the copy is dirty = M; GrantAck [S9.3.9]. Primer 8.2: IM_ad + Data(0 acks) -> M.
- (IM_a, Grant_denied) -> `Error/GrantAck/I`: No effect [S9.3.7]: the master's permission is None (it had none, or it was pruned BtoN by the Probe that brought it here). The parked Store completes with an error; GrantAck still sent [TL-DENY].
- (IM_a, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row SM_a

- (SM_a, Load) -> `Hit/`: Hit: a Branch has read permission [Table 29] and the pending BtoT Acquire does not remove it. Primer 8.2: SM_ad + load -> hit.
- (SM_a, Store) -> `Stall/`: Stall: one Acquire per block (plan S1.4, strict reading of [S9.2-A]); the access waits for the Grant. The first Store is parked; later ones wait.
- (SM_a, Replacement) -> `Stall/`: Stall: a Release may not be issued while a Grant is pending [S9.2-R]; the eviction waits for the Grant (as SM_ad/IS_d + Replacement = Stall in MESI_directory_v2).
- (SM_a, ProbeBlock_toN) -> `ProbeAck_BtoN/IM_a`: ProbeAck BtoN [Prune, S9.3.5]: the Branch is pruned while the BtoT Acquire is outstanding (the Probe must be answered, Figure 29 step 3). The master now has None, so the pending Acquire can only be completed by GrantData toT [S9.2: 'Slaves must include an up-to-date copy ... unless certain the master has not been probed']; IM_a is that expectation (Grant_toT without data is a Fault there). M-PERM: BtoN starts from the tracked B.
- (SM_a, ProbeBlock_toB) -> `ProbeAck_BtoB/`: ProbeAck BtoB [Report]: the cap is what the master already holds, 'does not result in a permissions change' [S9.3.3]. The Acquire stays pending and still expects Grant or GrantData toT.
- (SM_a, ProbeBlock_toT) -> `ProbeAck_BtoB/`: ProbeAck BtoB [Report]: a cap more permissive than the current permission changes nothing [S9.3.3]. (The manager table never sends toT; kept for a conforming foreign manager.)
- (SM_a, Grant_toB) -> `Fault/`: Fault: toB does not meet BtoT [D7, M-PARAM].
- (SM_a, Grant_toT) -> `Hit/GrantAck/M`: Grant toT without data meets BtoT [D7]: legal only because this master has not been probed since the Acquire (a Probe toN would have moved it to IM_a) and the manager knows it [S9.2 Figure 29 step 8 contrapositive]; the Branch data is current (no writer existed while it held B). Store performed (Hit) -> M; GrantAck. Primer 8.2: SM_ad + Data -> M.
- (SM_a, GrantData_toB) -> `Fault/`: Fault: toB does not meet BtoT [D7, M-PARAM].
- (SM_a, GrantData_toT) -> `Hit/GrantAck/M`: GrantData toT meets BtoT; the manager was not certain the master was unprobed and included the data [S9.2]. The data overwrites a copy that is identical or stale, both safe. Hit -> M; GrantAck.
- (SM_a, Grant_denied) -> `Error/GrantAck/S`: No effect [S9.3.7]: the master keeps its Branch (it was not probed, else it would be in IM_a). The parked Store completes with an error; GrantAck still sent [TL-DENY].
- (SM_a, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row S

- (S, Load) -> `Hit/`: Hit: Branch = read permission [Table 29].
- (S, Store) -> `AcquireBlock_BtoT/SM_a`: AcquireBlock BtoT [Grow]: upgrade a Branch to Tip. M-PERM: BtoT from tracked B. M-CONC: nothing pending on the block. Primer 8.2: S + store -> GetM.
- (S, Replacement) -> `Release_BtoN/SI_r`: Release BtoN [Prune, S9.3.10]: a clean Branch is relinquished without data. M-CONC: no Grant pending in S [S9.2-R]. Primer 8.2: S + replacement -> PutS.
- (S, ProbeBlock_toN) -> `ProbeAck_BtoN/I`: ProbeAck BtoN [Prune]: Branch pruned, no data (a Branch is never dirty) -> I. Primer 8.2: S + Inv -> I.
- (S, ProbeBlock_toB) -> `ProbeAck_BtoB/`: ProbeAck BtoB [Report]: already capped at Branch, no change [S9.3.3].
- (S, ProbeBlock_toT) -> `ProbeAck_BtoB/`: ProbeAck BtoB [Report]: cap above current permission, no change [S9.3.3].
- (S, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (S, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (S, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (S, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (S, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (S, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row E

- (E, Load) -> `Hit/`: Hit: Tip with no Branches has read permission [Table 29].
- (E, Store) -> `Hit/M`: Hit -> M: a Tip with no Branches may write [Table 29]; no message. Primer 8.5: E + store -> M silently. Clean/dirty is L1 policy state on top of T (plan S5.1).
- (E, Replacement) -> `Release_TtoN/EI_r`: Release TtoN [Prune, S9.3.10] without data: the copy is clean, the manager (inclusive) holds the same bytes. M-CONC: no Grant pending [S9.2-R]. Primer 8.5: E + replacement -> PutE (no data).
- (E, ProbeBlock_toN) -> `ProbeAck_TtoN/I`: ProbeAck TtoN [Prune] without data: clean, the manager's copy is current -> I. Primer 8.5: E + Inv/Fwd-GetM -> I with no data needed from a clean copy (the manager supplies).
- (E, ProbeBlock_toB) -> `ProbeAck_TtoB/S`: ProbeAck TtoB [Prune] without data: capped at Branch, clean -> S. Primer 8.5: E + Fwd-GetS -> S.
- (E, ProbeBlock_toT) -> `ProbeAck_TtoT/`: ProbeAck TtoT [Report]: cap at Tip = current permission, no change [S9.3.3]; no data moves (ProbeAckData TtoT is never emitted, plan S5.3 I-C).
- (E, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (E, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (E, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (E, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (E, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (E, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row M

- (M, Load) -> `Hit/`: Hit: Tip with no Branches [Table 29].
- (M, Store) -> `Hit/`: Hit: Tip with no Branches may write [Table 29].
- (M, Replacement) -> `ReleaseData_TtoN/MI_r`: ReleaseData TtoN [Prune, S9.3.11]: dirty data is written back with the Release. M-CONC: no Grant pending [S9.2-R]. Primer 8.2: M + replacement -> PutM with data.
- (M, ProbeBlock_toN) -> `ProbeAckData_TtoN/I`: ProbeAckData TtoN [Prune, S9.3.6]: the dirty data goes to the manager (never to the requester: four-hop) -> I. Primer 8.2: M + Fwd-GetM/Inv -> I with data.
- (M, ProbeBlock_toB) -> `ProbeAckData_TtoB/S`: ProbeAckData TtoB [Prune]: dirty data to the manager, copy kept as a clean Branch -> S (the manager now holds the bytes). Primer 8.2: M + Fwd-GetS -> S with data; TileLink sends it to the manager, not the requester (plan S5.1).
- (M, ProbeBlock_toT) -> `ProbeAck_TtoT/`: ProbeAck TtoT [Report]: no change [S9.3.3]; the dirty data stays (ProbeAckData TtoT would need per-address ordering of C [S9.2-PAD], so it is never used).
- (M, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (M, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (M, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (M, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (M, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (M, ReleaseAck) -> `Fault/`: Fault: no Release is outstanding in this state, so no ReleaseAck can arrive (Table 30).

#### Row SI_r

- (SI_r, Load) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (SI_r, Store) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (SI_r, Replacement) -> `Fault/`: Fault: the line is already being released; the controller raises one Replacement per victim.
- (SI_r, ProbeBlock_toN) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (SI_r, ProbeBlock_toB) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (SI_r, ProbeBlock_toT) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (SI_r, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (SI_r, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (SI_r, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (SI_r, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (SI_r, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (SI_r, ReleaseAck) -> `I`: ReleaseAck completes the Release BtoN [S9.3.12]: the copy is gone -> I. Any parked Probe is now re-decoded in I and answered NtoN; any parked access re-acquires from I. Primer 8.2: MI_a/SI_a + Put-Ack -> I.

#### Row EI_r

- (EI_r, Load) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (EI_r, Store) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (EI_r, Replacement) -> `Fault/`: Fault: the line is already being released; the controller raises one Replacement per victim.
- (EI_r, ProbeBlock_toN) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (EI_r, ProbeBlock_toB) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (EI_r, ProbeBlock_toT) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (EI_r, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (EI_r, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (EI_r, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (EI_r, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (EI_r, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (EI_r, ReleaseAck) -> `I`: ReleaseAck completes the Release TtoN [S9.3.12]: the copy is gone -> I. Any parked Probe is now re-decoded in I and answered NtoN; any parked access re-acquires from I. Primer 8.2: MI_a/SI_a + Put-Ack -> I.

#### Row MI_r

- (MI_r, Load) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (MI_r, Store) -> `Stall/`: Stall: the block is being released; a new Acquire may not be issued until the ReleaseAck [S9.2-R]. The access is re-decoded in I.
- (MI_r, Replacement) -> `Fault/`: Fault: the line is already being released; the controller raises one Replacement per victim.
- (MI_r, ProbeBlock_toN) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (MI_r, ProbeBlock_toB) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (MI_r, ProbeBlock_toT) -> `Stall/`: Stall: a Release is outstanding; the master must not send ProbeAck until the ReleaseAck [S9.2-R]. The Probe is parked, not refused (rule 3: a higher-priority C message is unanswered, so withholding B is allowed). It is re-decoded in I after the ReleaseAck and answered NtoN there.
- (MI_r, Grant_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (MI_r, Grant_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (MI_r, GrantData_toB) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (MI_r, GrantData_toT) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (MI_r, Grant_denied) -> `Fault/`: Fault: no Acquire is outstanding in this state, so no Grant can arrive (a Grant answers an Acquire, Table 30).
- (MI_r, ReleaseAck) -> `I`: ReleaseAck completes the ReleaseData TtoN [S9.3.12]: the copy is gone -> I. Any parked Probe is now re-decoded in I and answered NtoN; any parked access re-acquires from I. Primer 8.2: MI_a/SI_a + Put-Ack -> I.
## 3. `TileLink/tables/TLC_MESI_LLC.csv`: the inclusive manager

### 3.1 States

Per block. "LLC copy" is the line in the LLC data array. Holders are the inner masters the directory
lists: at most one Tip (owner) or a set of Branches (sharers), never both (a Tip at this level has no
sibling Branches: an L1 that is the Tip has no readers above it).

| state | stable | holders | LLC copy | meaning |
|---|---|---|---|---|
| N | yes | none | none | nothing cached at any level |
| I | yes | none | current | LLC copy only |
| B | yes | Branches | current | read-sharing |
| T | yes | one Tip | possibly stale | the Tip may be dirty (M) or clean (E); the manager learns which from ProbeAck versus ProbeAckData |
| NT_d | no | none | being fetched | Get out for an Acquire; the Grant will be toT |
| T_g | no | the new Tip | current | Grant[Data] toT sent; waiting GrantAck; block blocked |
| B_g | no | Branches incl. requester | current | GrantData toB sent; waiting GrantAck; block blocked |
| N_g | no | none | none | denied Grant sent; waiting GrantAck |
| BT_p | no | Branches being probed | current | ProbeBlock toN to every Branch for a NtoT (or pruned BtoT) Acquire; then GrantData toT |
| BT_pu | no | Branches being probed + the unprobed requester | current | ProbeBlock toN to every other Branch for a BtoT upgrade; then Grant toT without data |
| TB_p | no | Tip being probed | possibly stale | ProbeBlock toB to the owner for a NtoB Acquire; then GrantData toB |
| TT_p | no | Tip being probed | possibly stale | ProbeBlock toN to the owner for a NtoT (or pruned BtoT) Acquire; then GrantData toT |
| BN_e | no | Branches being probed | current | LLC eviction: ProbeBlock toN to every Branch, then PutFullData out |
| TN_e | no | Tip being probed | possibly stale | LLC eviction: ProbeBlock toN to the owner, then PutFullData out |
| N_u | no | none | none | an uncached Get/Put/Atomic forwarded outward; waiting its AccessAck[Data] |
| TB_u | no | Tip being probed | possibly stale | uncached Get: ProbeBlock toB to the owner, then AccessAckData |
| BI_u | no | Branches being probed | current | uncached Put/Atomic: ProbeBlock toN to every Branch, then execute |
| TI_u | no | Tip being probed | possibly stale | uncached Put/Atomic: ProbeBlock toN to the owner, then execute |
| BN_i | no | Branches being probed | current | CBOInval: ProbeBlock toN to every Branch, then drop |
| TN_i | no | Tip being probed | possibly stale | CBOInval: ProbeBlock toN to the owner, then drop |
| TB_c | no | Tip being probed | possibly stale | CBOClean: ProbeBlock toB to the owner, then write outward |
| B_w | no | Branches | current, write outstanding | CBOClean: PutFullData out, waiting AccessAck, then HintAck |
| I_w | no | none | current, write outstanding | CBOClean: PutFullData out, waiting AccessAck, then HintAck |
| BN_f | no | Branches being probed | current | CBOFlush: ProbeBlock toN to every Branch, then write outward |
| TN_f | no | Tip being probed | possibly stale | CBOFlush: ProbeBlock toN to the owner, then write outward |
| N_w | no | none | current, write outstanding | CBOFlush: PutFullData out, waiting AccessAck, then drop and HintAck |

MSI manager (`TLC_MSI_LLC.csv`, written in T6): this file with two cells changed and one state appended.
Everything else is identical, because E is invisible to the manager (a clean Tip answers Probes with
ProbeAck, a dirty one with ProbeAckData, in both protocols).
- `I + AcquireBlock_NtoB` = `AddSharer/GrantData_toB/B_g` instead of `SetOwner/GrantData_toT/T_g`: the
  reader becomes a Branch, not an exclusive Tip. GrantData toB meets NtoB (D7).
- `N + AcquireBlock_NtoB` = `Get_out/NB_d` instead of `Get_out/NT_d`: `NT_d` serves NtoB and NtoT alike
  and always grants toT, so a cold read would still reach E. `NB_d` (StateNum 26, appended last, as
  FSMReader's row order requires) is `NT_d` row for row except `NB_d + AccessAckData` =
  `SaveData/AddSharer/GrantData_toB/B_g`.
- NtoT, BtoT and `BtoT_nonholder` are unchanged: they must be granted toT (D7). An earlier draft of
  this paragraph also changed `I + AcquireBlock_BtoT_nonholder` to toB; that was wrong (a toB Grant to
  a BtoT violates D7, and `IM_a + GrantData_toB` is a Fault at the L1).
TL-COH-ENGINE asserts that exactly these two cells and `NB_d` differ; under MSI no L1 ever enters E
(TL-FUZZ-C).

### 3.2 Events and their decode

| event | channel | decode |
|---|---|---|
| AcquireBlock_NtoB | inner A | opcode AcquireBlock, `param` NtoB. From a listed holder it is a decode fault (M-PERM: NtoB starts from N) |
| AcquireBlock_NtoT | inner A | opcode AcquireBlock, `param` NtoT; same holder check |
| AcquireBlock_BtoT | inner A | `param` BtoT, the requester is a listed Branch and at least one other Branch is listed |
| AcquireBlock_BtoT_last | inner A | `param` BtoT, the requester is the only listed holder |
| AcquireBlock_BtoT_nonholder | inner A | `param` BtoT, the requester is not listed (it answered BtoN to a Probe after issuing the Acquire: Figure 29) |
| ProbeAck_toN | inner C | opcode ProbeAck, `param` in {TtoN, BtoN, NtoN}, counter > 1 before this one |
| last_ProbeAck_toN | inner C | same, counter = 1 |
| ProbeAckData_toN | inner C | opcode ProbeAckData, `param` TtoN; the counter must be 1 (else a decode fault: data can only come from the single probed Tip) |
| ProbeAck_toB | inner C | opcode ProbeAck, `param` TtoB; counter must be 1 |
| ProbeAckData_toB | inner C | opcode ProbeAckData, `param` TtoB; counter must be 1 |
| (decode faults) | inner C | ProbeAck TtoT / BtoB (the manager never probes toT, and a toB probe goes only to a Tip, which cannot report BtoB); ProbeAckData BtoN / BtoB / TtoT (Table 29: no dirty Branch; TtoT with data never used, S9.2-PAD); any ProbeAck from a master that is not listed and not NtoN |
| GrantAck | inner E | `sink` matches the unanswered Grant of the block |
| Release_BtoN | inner C | opcode Release, `param` BtoN, sender a listed Branch, other holders listed |
| last_Release_BtoN | inner C | same, sender the only listed holder |
| Release_TtoN | inner C | opcode Release, `param` TtoN, sender the listed Tip |
| ReleaseData_TtoN | inner C | opcode ReleaseData, `param` TtoN, sender the listed Tip |
| (decode faults) | inner C | Release/ReleaseData with TtoB, BtoB, TtoT (not emitted by the L1 table; a foreign master's voluntary downgrade is not supported, S9), NtoN (illegal, S9.3.10), or from a master not listed |
| Replacement | self | the LLC's victim message |
| AccessAckData / _denied | outer D | opcode AccessAckData answering the Get of NT_d or the forwarded access of N_u; `denied` 0 / 1 |
| AccessAck / _denied | outer D | opcode AccessAck answering the PutFullData of a CMO row or the forwarded Put of N_u; the AccessAck of an eviction's PutFullData is consumed by the port (S1.2 rule 7) and never reaches the table |
| Get_u | inner A | opcode Get (an uncached master; the L1 table never sends it) |
| PutAtomic_u | inner A | opcode PutFullData, PutPartialData, ArithmeticData or LogicalData; the opcode and param are kept with the parked request for `Execute` / `ForwardOut` |
| Intent_Prefetch | inner A | opcode Intent, `param` PrefetchRead (0) or PrefetchWrite (1) |
| Intent_CBOInval / _CBOClean / _CBOFlush | inner A | opcode Intent, `param` 5 / 6 / 7 |
| (decode faults) | inner A | AcquirePerm (not supported, S9), ProbePerm never arrives on A |

### 3.3 Actions and what they emit

| action | emits | channel | param / fields | data | count |
|---|---|---|---|---|---|
| Get_out | Get | outer A | size = block | no | 1 |
| PutFullData_out | PutFullData | outer A | size = block | the LLC copy | 1 |
| ForwardOut | the parked uncached access, unchanged opcode/param/size/mask | outer A | as received | as received | 1 |
| ForwardIn | AccessAck or AccessAckData | inner D | copied from the outer response, `denied` and `corrupt` as received | as received | 1 |
| GrantData_toT / GrantData_toB | GrantData | inner D | Cap toT (0) / toB (1), `denied` 0, new sink | the LLC copy | 1 |
| Grant_toT | Grant | inner D | Cap toT, `denied` 0, new sink | no | 1 |
| Grant_denied | Grant | inner D | `denied` 1, `param` = requested cap (S1.2 rule 8), new sink | no | 1 |
| ReleaseAck | ReleaseAck | inner D | `param` 0, `denied` 0 | no | 1 |
| ProbeBlock_toN_all | ProbeBlock | inner B | Cap toN, one per holder except the requester | no | holders (fan-out) |
| ProbeBlock_toN_owner | ProbeBlock | inner B | Cap toN, to the Tip | no | 1 |
| ProbeBlock_toB_owner | ProbeBlock | inner B | Cap toB, to the Tip | no | 1 |
| AccessAckData_in | AccessAckData | inner D | `denied` 0 | the LLC copy (masked to the request) | 1 |
| Execute | AccessAck (Put) or AccessAckData (Atomic: the old value) | inner D | `denied` 0 | per opcode | 1 |
| HintAck / HintAck_denied | HintAck | inner D | `denied` 0 / 1 | no | 1 |
| SaveData, Drop, SetOwner, AddSharer, OwnerToSharer, RemoveHolder, DecCount, Stall, Fault | nothing | | | | 0 |

### 3.4 Every cell


#### Row N

- (N, AcquireBlock_NtoB) -> `Get_out/NT_d`: Get_out [TL-UH Get on the outer link, full block] and park the Acquire: the LLC holds no copy, so the data must be fetched before any Grant (a Grant must carry the data, S9.3.8). NtoB and NtoT converge: with no other copy the Grant will be toT in both cases [S9.3.7 'may exceed'; D7; plan S5.1 MESI]. Primer 8.2: N(I at home) + GetS/GetM -> read memory.
- (N, AcquireBlock_NtoT) -> `Get_out/NT_d`: Get_out [TL-UH Get on the outer link, full block] and park the Acquire: the LLC holds no copy, so the data must be fetched before any Grant (a Grant must carry the data, S9.3.8). NtoB and NtoT converge: with no other copy the Grant will be toT in both cases [S9.3.7 'may exceed'; D7; plan S5.1 MESI]. Primer 8.2: N(I at home) + GetS/GetM -> read memory.
- (N, AcquireBlock_BtoT) -> `Fault/`: Fault: the decoder marks a BtoT as 'holder' only when the requester is a listed Branch; this state lists no Branch (M-PERM).
- (N, AcquireBlock_BtoT_last) -> `Fault/`: Fault: the decoder marks a BtoT as 'holder' only when the requester is a listed Branch; this state lists no Branch (M-PERM).
- (N, AcquireBlock_BtoT_nonholder) -> `Get_out/NT_d`: The BtoT requester is no longer a holder (it answered BtoN to a Probe after issuing the Acquire, Figure 29); it needs the data [S9.2 'include an up-to-date copy']: same as NtoT.
- (N, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (N, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N, Replacement) -> `Fault/`: Fault: nothing to evict in N (as N + Replacement = Fault in MESI_LLC_directory_v2).
- (N, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N, Get_u) -> `ForwardOut/N_u`: ForwardOut: no copy anywhere, so the uncached access is re-issued unchanged on the outer link and parked [D8: no holder to probe]; the LLC does not allocate on an uncached access (inclusion concerns inner copies only). The port's H1 hold orders it after any unacknowledged write-back of the block.
- (N, PutAtomic_u) -> `ForwardOut/N_u`: ForwardOut: no copy anywhere, so the uncached access is re-issued unchanged on the outer link and parked [D8: no holder to probe]; the LLC does not allocate on an uncached access (inclusion concerns inner copies only). The port's H1 hold orders it after any unacknowledged write-back of the block.
- (N, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (N, Intent_CBOInval) -> `HintAck/`: HintAck at once: no copy at any level, nothing to invalidate, clean or flush; the completion semantics of S8.2.3 hold trivially.
- (N, Intent_CBOClean) -> `HintAck/`: HintAck at once: no copy at any level, nothing to invalidate, clean or flush; the completion semantics of S8.2.3 hold trivially.
- (N, Intent_CBOFlush) -> `HintAck/`: HintAck at once: no copy at any level, nothing to invalidate, clean or flush; the completion semantics of S8.2.3 hold trivially.

#### Row I

- (I, AcquireBlock_NtoB) -> `SetOwner/GrantData_toT/T_g`: SetOwner/GrantData_toT/T_g: no inner copy exists, so the requester becomes the Tip (E at the L1 on a read: plan S5.1 MESI, Primer 8.5 'exclusive data when no sharers'); GrantData carries the LLC copy [S9.3.8]; the block is blocked until the GrantAck [S9.2-G]. M-CONC: no ProbeAck pending in I.
- (I, AcquireBlock_NtoT) -> `SetOwner/GrantData_toT/T_g`: SetOwner/GrantData_toT/T_g: no inner copy exists, so the requester becomes the Tip (E at the L1 on a read: plan S5.1 MESI, Primer 8.5 'exclusive data when no sharers'); GrantData carries the LLC copy [S9.3.8]; the block is blocked until the GrantAck [S9.2-G]. M-CONC: no ProbeAck pending in I.
- (I, AcquireBlock_BtoT) -> `Fault/`: Fault: the decoder marks a BtoT as 'holder' only when the requester is a listed Branch; this state lists no Branch (M-PERM).
- (I, AcquireBlock_BtoT_last) -> `Fault/`: Fault: the decoder marks a BtoT as 'holder' only when the requester is a listed Branch; this state lists no Branch (M-PERM).
- (I, AcquireBlock_BtoT_nonholder) -> `SetOwner/GrantData_toT/T_g`: The requester was pruned to None after issuing BtoT (Figure 29 step 8): it must receive the data; SetOwner/GrantData_toT/T_g: no inner copy exists, so the requester becomes the Tip (E at the L1 on a read: plan S5.1 MESI, Primer 8.5 'exclusive data when no sharers'); GrantData carries the LLC copy [S9.3.8]; the block is blocked until the GrantAck [S9.2-G]. M-CONC: no ProbeAck pending in I.
- (I, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (I, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (I, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (I, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (I, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (I, Replacement) -> `PutFullData_out/N`: PutFullData_out/N: the LLC copy is written outward (as MESI_LLC_directory_v2 I + Replacement = WriteBack/N; the data handler decides whether a clean line really transfers). The AccessAck of this write is consumed by the port (H1: later outer requests to the block wait for it), not by the table [plan S5.1 K_MEM_WACK].
- (I, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (I, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (I, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (I, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (I, Get_u) -> `AccessAckData_in/`: AccessAckData_in, stay: the LLC copy is the only copy, so it is current; answer from it [D8: no owner to probe]. S6.5 holds: no other copy can be stale.
- (I, PutAtomic_u) -> `Execute/`: Execute, stay: the LLC copy is the only copy; the Put is written into it / the atomic is applied to it and the AccessAck / AccessAckData is sent [D8: no holder to probe; S6.5: 'all current cached copies are up-to-date before the acknowledgement'].
- (I, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (I, Intent_CBOInval) -> `Drop/HintAck/N`: CBOInval [S8.2.3, plan S3.5]: no inner copy; the LLC copy is dropped, dirty data discarded, HintAck -> N.
- (I, Intent_CBOClean) -> `PutFullData_out/I_w`: CBOClean: write the LLC copy outward, keep it; the HintAck waits for the AccessAck [S8.2.3: HintAck carries completion].
- (I, Intent_CBOFlush) -> `PutFullData_out/N_w`: CBOFlush = Clean then drop: write outward; the copy is dropped when the AccessAck arrives (a denied write keeps it).

#### Row B

- (B, AcquireBlock_NtoB) -> `AddSharer/GrantData_toB/B_g`: AddSharer/GrantData_toB/B_g: readers share; the LLC copy is current (Branches are read-only, Table 29), so GrantData toB from it; the requester joins the sharers; block blocked until GrantAck [S9.2-G]. M-CONC: no ProbeAck pending. Primer 8.2: S(home) + GetS -> data, add sharer.
- (B, AcquireBlock_NtoT) -> `ProbeBlock_toN_all/BT_p`: ProbeBlock toN to every Branch [Cap, S9.3.3], park the Acquire -> BT_p: a writer needs Tip with no Branches [Table 29]; the Grant waits for the last ProbeAck [S9.2-G]. One Probe per master [S9.2-P]. Primer 8.2: S(home) + GetM -> Inv to sharers.
- (B, AcquireBlock_BtoT) -> `ProbeBlock_toN_all/BT_pu`: ProbeBlock toN to every Branch except the requester, park -> BT_pu: an upgrade with other Branches present. The requester is not probed (it is the one being served), so the manager is certain it stays unprobed and may later Grant without data [S9.2 'unless they are certain']. Primer 8.2: S(home) + GetM from a sharer -> Inv to the others.
- (B, AcquireBlock_BtoT_last) -> `SetOwner/Grant_toT/T_g`: SetOwner/Grant_toT/T_g: the requester is the only Branch; nothing to probe; Grant toT WITHOUT data is exact because the requester holds the current data and the manager has not probed it since its Acquire (no Probe is outstanding in B) [S9.2 Figure 29 step 8 contrapositive]. SetOwner clears the sharer set and names the owner. Primer 8.2: S(home) + GetM from the sole sharer -> ack, M.
- (B, AcquireBlock_BtoT_nonholder) -> `ProbeBlock_toN_all/BT_p`: The requester was pruned after issuing BtoT (Figure 29); treat as NtoT: probe every Branch toN, then GrantData toT [S9.2 'include an up-to-date copy'].
- (B, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (B, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch leaves (Release BtoN, Prune), others remain; ReleaseAck [S9.3.12]. Primer 8.2: S(home) + PutS -> Put-Ack, remove sharer.
- (B, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/I`: The last Branch leaves -> I (LLC copy only). Primer 8.2: S(home) + PutS-Last -> I.
- (B, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (B, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (B, Replacement) -> `ProbeBlock_toN_all/BN_e`: LLC eviction with inner Branches (inclusion, plan S3.5): ProbeBlock toN to every Branch 'of its own volition' [S9.3.3], then write back once every ProbeAck is in -> BN_e. Primer 8.2 home eviction: Inv to sharers.
- (B, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B, Get_u) -> `AccessAckData_in/`: AccessAckData_in, stay: Branches are read-only, the LLC copy is current; serve the uncached read from it, Branch holders stay [D8: Get probes only a writable owner].
- (B, PutAtomic_u) -> `ProbeBlock_toN_all/BI_u`: ProbeBlock toN to every Branch, park -> BI_u [D8: a Put/Atomic probes every holder toN so no stale copy survives the AccessAck, S6.5].
- (B, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (B, Intent_CBOInval) -> `ProbeBlock_toN_all/BN_i`: CBOInval: ProbeBlock toN to every Branch, park -> BN_i; the LLC copy is dropped and the HintAck sent after the last ProbeAck [plan S3.5].
- (B, Intent_CBOClean) -> `PutFullData_out/B_w`: CBOClean: the Branches are clean read-only copies, so only the LLC copy can be dirty; write it outward and keep every copy at Branch -> B_w. A Probe toB of a Branch would be a no-op [S9.3.3] and is omitted (deviation from the wording of plan S3.5, same result).
- (B, Intent_CBOFlush) -> `ProbeBlock_toN_all/BN_f`: CBOFlush: invalidate every Branch (ProbeBlock toN), then write the LLC copy outward and drop it -> BN_f.

#### Row T

- (T, AcquireBlock_NtoB) -> `ProbeBlock_toB_owner/TB_p`: ProbeBlock toB to the owner [Cap], park -> TB_p: the Tip is capped at Branch; if it is dirty its ProbeAckData TtoB brings the data to the manager (four-hop, plan S5.1); the Grant waits for the ProbeAck [S9.2-G]. Primer 8.2: M(home) + GetS -> Fwd-GetS.
- (T, AcquireBlock_NtoT) -> `ProbeBlock_toN_owner/TT_p`: ProbeBlock toN to the owner, park -> TT_p; GrantData toT after its ProbeAck[Data]. Primer 8.2: M(home) + GetM -> Fwd-GetM.
- (T, AcquireBlock_BtoT) -> `Fault/`: Fault: the only holder is the Tip; a BtoT from it would mean it believes it holds Branch (M-PERM: BtoT must start from tracked B).
- (T, AcquireBlock_BtoT_last) -> `Fault/`: Fault: as AcquireBlock_BtoT in T: the Tip is not a Branch.
- (T, AcquireBlock_BtoT_nonholder) -> `ProbeBlock_toN_owner/TT_p`: A pruned BtoT requester (Figure 29) is served like NtoT: probe the owner toN, then GrantData toT.
- (T, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (T, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (T, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (T, Release_TtoN) -> `RemoveHolder/ReleaseAck/I`: A clean Tip relinquishes its copy (Release TtoN, no data: the LLC copy is current because a clean Tip never wrote) -> I; ReleaseAck. Primer 8.5: E(home) + PutE -> I.
- (T, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/I`: A dirty Tip writes back and leaves (ReleaseData TtoN): save the data, clear the owner, ReleaseAck -> I. Primer 8.2: M(home) + PutM -> write data, I.
- (T, Replacement) -> `ProbeBlock_toN_owner/TN_e`: LLC eviction with a Tip inside (inclusion): ProbeBlock toN to the owner, then PutFullData outward after its ProbeAck[Data] -> TN_e [plan S3.5].
- (T, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T, Get_u) -> `ProbeBlock_toB_owner/TB_u`: Uncached Get with a writable owner: ProbeBlock toB to the owner, park -> TB_u; its ProbeAckData TtoB (if dirty) supplies the current data [D8; S6.5].
- (T, PutAtomic_u) -> `ProbeBlock_toN_owner/TI_u`: Uncached Put/Atomic with a Tip: ProbeBlock toN to the owner, park -> TI_u; execute after its ProbeAck[Data] [D8].
- (T, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (T, Intent_CBOInval) -> `ProbeBlock_toN_owner/TN_i`: CBOInval: ProbeBlock toN to the owner, park -> TN_i; its data (if any) is discarded, the LLC copy dropped, HintAck [plan S3.5].
- (T, Intent_CBOClean) -> `ProbeBlock_toB_owner/TB_c`: CBOClean: ProbeBlock toB to the owner, park -> TB_c; dirty data (ProbeAckData) is saved and written outward, the owner keeps a Branch [plan S3.5].
- (T, Intent_CBOFlush) -> `ProbeBlock_toN_owner/TN_f`: CBOFlush: ProbeBlock toN to the owner, park -> TN_f; then write the (updated) LLC copy outward and drop it.

#### Row NT_d

- (NT_d, AcquireBlock_NtoB) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, AcquireBlock_NtoT) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, AcquireBlock_BtoT) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, AcquireBlock_BtoT_last) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (NT_d, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (NT_d, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (NT_d, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (NT_d, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (NT_d, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (NT_d, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (NT_d, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (NT_d, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (NT_d, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (NT_d, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (NT_d, AccessAckData) -> `SaveData/SetOwner/GrantData_toT/T_g`: The fill arrived [TL-UH Get -> AccessAckData]: save it, the requester becomes the Tip, GrantData toT (exceeds NtoB, meets NtoT [D7]); block blocked until GrantAck [S9.2-G]. M-CONC (slave): no ProbeAckData pending, so the AccessAckData could be consumed [S9.2-AA]. Primer 8.2: home + data from memory -> send data.
- (NT_d, AccessAckData_denied) -> `Grant_denied/N_g`: The outer read was denied (every beat corrupt, S4.4): nothing is cached; the Acquire is answered with a denied Grant (param = the requested cap, never toN [D7]; permissions unchanged [S9.3.7]); the master still sends GrantAck, so the block stays blocked -> N_g [TL-DENY].
- (NT_d, AccessAck) -> `Fault/`: Fault: a Get is answered by AccessAckData only (Table 12).
- (NT_d, AccessAck_denied) -> `Fault/`: Fault: a Get is answered by AccessAckData only (Table 12).
- (NT_d, Get_u) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, PutAtomic_u) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (NT_d, Intent_CBOInval) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, Intent_CBOClean) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).
- (NT_d, Intent_CBOFlush) -> `Stall/`: Stall: an outer request issued for this block is outstanding; a second one must not be issued (one transaction per block; and S6.5 orders a later access after the response of the earlier one).

#### Row T_g

- (T_g, AcquireBlock_NtoB) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, AcquireBlock_NtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, AcquireBlock_BtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, AcquireBlock_BtoT_last) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T_g, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T_g, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T_g, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T_g, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (T_g, GrantAck) -> `T`: GrantAck [S9.3.9] closes the transaction: the requester is the Tip; the block is unblocked -> T (Figure 29 step 6).
- (T_g, Release_BtoN) -> `Fault/`: Fault: every former Branch was probed to None before the Grant [S9.2-G] and the new Tip is not a Branch (M-PERM).
- (T_g, last_Release_BtoN) -> `Fault/`: Fault: every former Branch was probed to None before the Grant [S9.2-G] and the new Tip is not a Branch (M-PERM).
- (T_g, Release_TtoN) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel. A Release here comes from a Branch the Grant did not touch, or from the requester whose Release on C overtook its GrantAck on E (channels are not mutually ordered, S9.2); either way it is served after the GrantAck. The releasing master only waits for the ReleaseAck and holds no ProbeAck anyone needs (no Probe is outstanding).
- (T_g, ReleaseData_TtoN) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel. A Release here comes from a Branch the Grant did not touch, or from the requester whose Release on C overtook its GrantAck on E (channels are not mutually ordered, S9.2); either way it is served after the GrantAck. The releasing master only waits for the ReleaseAck and holds no ProbeAck anyone needs (no Probe is outstanding).
- (T_g, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (T_g, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T_g, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T_g, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T_g, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (T_g, Get_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, PutAtomic_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (T_g, Intent_CBOInval) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, Intent_CBOClean) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (T_g, Intent_CBOFlush) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.

#### Row B_g

- (B_g, AcquireBlock_NtoB) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, AcquireBlock_NtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, AcquireBlock_BtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, AcquireBlock_BtoT_last) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_g, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_g, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_g, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_g, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_g, GrantAck) -> `B`: GrantAck closes the transaction: the requester is a Branch among the sharers -> B.
- (B_g, Release_BtoN) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel. A Release here comes from a Branch the Grant did not touch, or from the requester whose Release on C overtook its GrantAck on E (channels are not mutually ordered, S9.2); either way it is served after the GrantAck. The releasing master only waits for the ReleaseAck and holds no ProbeAck anyone needs (no Probe is outstanding).
- (B_g, last_Release_BtoN) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel. A Release here comes from a Branch the Grant did not touch, or from the requester whose Release on C overtook its GrantAck on E (channels are not mutually ordered, S9.2); either way it is served after the GrantAck. The releasing master only waits for the ReleaseAck and holds no ProbeAck anyone needs (no Probe is outstanding).
- (B_g, Release_TtoN) -> `Fault/`: Fault: in B_g the directory lists Branches only (the Grant was toB), so no Tip can release (M-PERM).
- (B_g, ReleaseData_TtoN) -> `Fault/`: Fault: in B_g the directory lists Branches only (the Grant was toB), so no Tip can release (M-PERM).
- (B_g, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (B_g, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B_g, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B_g, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B_g, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (B_g, Get_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, PutAtomic_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (B_g, Intent_CBOInval) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, Intent_CBOClean) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (B_g, Intent_CBOFlush) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.

#### Row N_g

- (N_g, AcquireBlock_NtoB) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, AcquireBlock_NtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, AcquireBlock_BtoT) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, AcquireBlock_BtoT_last) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_g, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_g, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_g, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_g, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_g, GrantAck) -> `N`: GrantAck to the denied Grant: no copy was created -> N [TL-DENY: 'the GrantAck still sent']
- (N_g, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_g, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_g, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_g, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_g, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (N_g, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N_g, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N_g, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N_g, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (N_g, Get_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, PutAtomic_u) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (N_g, Intent_CBOInval) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, Intent_CBOClean) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.
- (N_g, Intent_CBOFlush) -> `Stall/`: Stall: the block is blocked from Grant to GrantAck [S9.2-G: 'should not issue Probes on that block until it receives a GrantAck'; Figure 29 step 5]. The message is parked; it is re-decoded after the GrantAck. Deadlock-free: the GrantAck is an E message the master sends unconditionally and E is the highest-priority channel.

#### Row BT_p

- (BT_p, AcquireBlock_NtoB) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, AcquireBlock_NtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, AcquireBlock_BtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, AcquireBlock_BtoT_last) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BT_p, last_ProbeAck_toN) -> `RemoveHolder/DecCount/SetOwner/GrantData_toT/T_g`: The last Branch is gone: every ProbeAck is in, so the Grant may be issued [S9.2-G]; the requester (None) becomes the Tip and gets the data (GrantData toT) [S9.2 Figure 29 step 8] -> T_g. Primer 8.2: home + last Inv-Ack -> data to requester, M.
- (BT_p, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BT_p, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BT_p, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BT_p, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BT_p, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted.
- (BT_p, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. Even when it is the last listed Branch the manager keeps waiting for the counted ProbeAck.
- (BT_p, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BT_p, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BT_p, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BT_p, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_p, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_p, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_p, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_p, Get_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, PutAtomic_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BT_p, Intent_CBOInval) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, Intent_CBOClean) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_p, Intent_CBOFlush) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.

#### Row BT_pu

- (BT_pu, AcquireBlock_NtoB) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, AcquireBlock_NtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, AcquireBlock_BtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, AcquireBlock_BtoT_last) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BT_pu, last_ProbeAck_toN) -> `RemoveHolder/DecCount/SetOwner/Grant_toT/T_g`: Every other Branch is gone: Grant toT WITHOUT data is exact because the requester was never probed (the manager probed 'all except the requester') and holds the current bytes [S9.2 'unless they are certain that that master has not been probed'] -> T_g. Primer 8.2: home + last Inv-Ack for an upgrade -> ack, M.
- (BT_pu, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BT_pu, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BT_pu, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BT_pu, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BT_pu, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. (The requester cannot be the sender: it has an Acquire pending and may not Release [S9.2-R].)
- (BT_pu, last_Release_BtoN) -> `Fault/`: Fault: the requester is a listed Branch with an Acquire pending, so no other Branch can be the last holder.
- (BT_pu, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BT_pu, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BT_pu, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BT_pu, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_pu, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_pu, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_pu, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BT_pu, Get_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, PutAtomic_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BT_pu, Intent_CBOInval) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, Intent_CBOClean) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (BT_pu, Intent_CBOFlush) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.

#### Row TB_p

- (TB_p, AcquireBlock_NtoB) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, AcquireBlock_NtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, AcquireBlock_BtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, AcquireBlock_BtoT_last) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TB_p, last_ProbeAck_toN) -> `RemoveHolder/DecCount/AddSharer/GrantData_toB/B_g`: The owner answered TtoN (pruned further than asked, allowed: Prune) or NtoN (it released first, Figure 30, and the data came with the ReleaseData): it is gone, the LLC copy is current; GrantData toB meets NtoB [D7] -> B_g with the requester as the only Branch. (toT would also be legal; toB is chosen so the Grant depends only on the request.)
- (TB_p, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/AddSharer/GrantData_toB/B_g`: The owner wrote back with ProbeAckData TtoN: save, remove it, GrantData toB from the fresh copy -> B_g.
- (TB_p, ProbeAck_toB) -> `OwnerToSharer/DecCount/AddSharer/GrantData_toB/B_g`: ProbeAck TtoB without data: the owner was clean (E), so the LLC copy is current; the owner becomes a Branch, the requester another; GrantData toB -> B_g [S9.2-G: Grant after the ProbeAck]. Primer 8.5: E(home) + GetS -> Fwd-GetS, S with two sharers.
- (TB_p, ProbeAckData_toB) -> `SaveData/OwnerToSharer/DecCount/AddSharer/GrantData_toB/B_g`: ProbeAckData TtoB: the dirty owner wrote back to the manager (four-hop) and kept a Branch; save, then GrantData toB from the saved data -> B_g. Primer 8.2: M(home) + GetS -> data from owner to requester and home; here both legs go through the manager.
- (TB_p, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TB_p, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_p, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_p, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TB_p, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TB_p, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TB_p, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_p, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_p, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_p, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_p, Get_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, PutAtomic_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TB_p, Intent_CBOInval) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, Intent_CBOClean) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TB_p, Intent_CBOFlush) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.

#### Row TT_p

- (TT_p, AcquireBlock_NtoB) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, AcquireBlock_NtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, AcquireBlock_BtoT) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, AcquireBlock_BtoT_last) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TT_p, last_ProbeAck_toN) -> `RemoveHolder/DecCount/SetOwner/GrantData_toT/T_g`: The owner answered TtoN without data (clean) or NtoN (released first, Figure 30): the LLC copy is current; the requester becomes the Tip with GrantData toT -> T_g. Primer 8.5: E(home) + GetM -> Fwd-GetM, M.
- (TT_p, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/SetOwner/GrantData_toT/T_g`: ProbeAckData TtoN: dirty data to the manager, then GrantData toT to the requester (the data travels owner -> manager -> requester, plan S5.1) -> T_g. Primer 8.2: M(home) + GetM -> Fwd-GetM, data to requester.
- (TT_p, ProbeAck_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TT_p, ProbeAckData_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TT_p, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TT_p, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TT_p, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TT_p, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TT_p, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TT_p, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TT_p, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TT_p, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TT_p, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TT_p, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TT_p, Get_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, PutAtomic_u) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TT_p, Intent_CBOInval) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, Intent_CBOClean) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.
- (TT_p, Intent_CBOFlush) -> `Stall/`: Stall: one transaction per block at the manager while Probes are outstanding [S9.2-G: no Grant while a ProbeAck is pending; S9.2-P: no second Probe to a master until its ProbeAck]. Parked, re-decoded when the block returns to a stable state.

#### Row BN_e

- (BN_e, AcquireBlock_NtoB) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, AcquireBlock_NtoT) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, AcquireBlock_BtoT) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, AcquireBlock_BtoT_last) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BN_e, last_ProbeAck_toN) -> `RemoveHolder/DecCount/PutFullData_out/N`: Every Branch is gone: write the LLC copy outward (PutFullData, its AccessAck goes to the port: H1) and free the block -> N. Primer 8.2 home eviction: after the last Inv-Ack, write back.
- (BN_e, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BN_e, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_e, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_e, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BN_e, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted.
- (BN_e, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. The counted ProbeAck is still awaited.
- (BN_e, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_e, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_e, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BN_e, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_e, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_e, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_e, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_e, Get_u) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, PutAtomic_u) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BN_e, Intent_CBOInval) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, Intent_CBOClean) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (BN_e, Intent_CBOFlush) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].

#### Row TN_e

- (TN_e, AcquireBlock_NtoB) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, AcquireBlock_NtoT) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, AcquireBlock_BtoT) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, AcquireBlock_BtoT_last) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TN_e, last_ProbeAck_toN) -> `RemoveHolder/DecCount/PutFullData_out/N`: The Tip is gone (TtoN clean, or NtoN after its ReleaseData/Release was merged): the LLC copy is current; write it outward -> N.
- (TN_e, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/PutFullData_out/N`: ProbeAckData TtoN: the dirty data arrives, is saved and written outward -> N.
- (TN_e, ProbeAck_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_e, ProbeAckData_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_e, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TN_e, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_e, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_e, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TN_e, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TN_e, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TN_e, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_e, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_e, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_e, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_e, Get_u) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, PutAtomic_u) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TN_e, Intent_CBOInval) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, Intent_CBOClean) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].
- (TN_e, Intent_CBOFlush) -> `Stall/`: Stall: the block is being evicted (Probes out, then PutFullData); the request is re-decoded in N after the eviction and is served by a fresh outer Get [S6.5: the write-back is ordered before it].

#### Row N_u

- (N_u, AcquireBlock_NtoB) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, AcquireBlock_NtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, AcquireBlock_BtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, AcquireBlock_BtoT_last) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_u, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_u, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_u, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_u, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_u, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (N_u, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_u, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_u, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_u, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_u, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (N_u, AccessAckData) -> `ForwardIn/N`: ForwardIn/N: the outer response (AccessAck for a Put, AccessAckData for a Get or an Atomic, denied copied as is [S4.4]) is relayed to the parked uncached requester; no copy was created -> N.
- (N_u, AccessAckData_denied) -> `ForwardIn/N`: ForwardIn/N: the outer response (AccessAck for a Put, AccessAckData for a Get or an Atomic, denied copied as is [S4.4]) is relayed to the parked uncached requester; no copy was created -> N.
- (N_u, AccessAck) -> `ForwardIn/N`: ForwardIn/N: the outer response (AccessAck for a Put, AccessAckData for a Get or an Atomic, denied copied as is [S4.4]) is relayed to the parked uncached requester; no copy was created -> N.
- (N_u, AccessAck_denied) -> `ForwardIn/N`: ForwardIn/N: the outer response (AccessAck for a Put, AccessAckData for a Get or an Atomic, denied copied as is [S4.4]) is relayed to the parked uncached requester; no copy was created -> N.
- (N_u, Get_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, PutAtomic_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (N_u, Intent_CBOInval) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, Intent_CBOClean) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (N_u, Intent_CBOFlush) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].

#### Row TB_u

- (TB_u, AcquireBlock_NtoB) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, AcquireBlock_NtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, AcquireBlock_BtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, AcquireBlock_BtoT_last) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TB_u, last_ProbeAck_toN) -> `RemoveHolder/DecCount/AccessAckData_in/I`: The owner left (TtoN clean, or NtoN after a merged Release): the LLC copy is current; answer the uncached Get from it -> I.
- (TB_u, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/AccessAckData_in/I`: The owner wrote back and left: save, answer the Get from the fresh copy -> I.
- (TB_u, ProbeAck_toB) -> `OwnerToSharer/DecCount/AccessAckData_in/B`: The clean owner is now a Branch: the LLC copy is current; AccessAckData to the uncached requester -> B [D8: Get probes the owner toB; Branches stay]. S9.2-AA: no ProbeAckData is pending any more.
- (TB_u, ProbeAckData_toB) -> `SaveData/OwnerToSharer/DecCount/AccessAckData_in/B`: The dirty owner wrote back and kept a Branch: save, AccessAckData from the saved data -> B. S9.2-AA: the AccessAckData is issued only after the ProbeAckData arrived.
- (TB_u, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TB_u, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_u, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_u, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TB_u, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TB_u, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TB_u, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_u, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_u, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_u, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_u, Get_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, PutAtomic_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TB_u, Intent_CBOInval) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, Intent_CBOClean) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TB_u, Intent_CBOFlush) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].

#### Row BI_u

- (BI_u, AcquireBlock_NtoB) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, AcquireBlock_NtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, AcquireBlock_BtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, AcquireBlock_BtoT_last) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BI_u, last_ProbeAck_toN) -> `RemoveHolder/DecCount/Execute/I`: Every Branch is invalidated: the LLC copy is the only copy; execute the parked Put/Atomic on it and acknowledge (AccessAck / AccessAckData) -> I [D8; S6.5: no stale copy survives the acknowledgement].
- (BI_u, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BI_u, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BI_u, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BI_u, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BI_u, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted.
- (BI_u, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. The counted ProbeAck is still awaited.
- (BI_u, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BI_u, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BI_u, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BI_u, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BI_u, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BI_u, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BI_u, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BI_u, Get_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, PutAtomic_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BI_u, Intent_CBOInval) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, Intent_CBOClean) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (BI_u, Intent_CBOFlush) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].

#### Row TI_u

- (TI_u, AcquireBlock_NtoB) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, AcquireBlock_NtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, AcquireBlock_BtoT) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, AcquireBlock_BtoT_last) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TI_u, last_ProbeAck_toN) -> `RemoveHolder/DecCount/Execute/I`: The Tip is gone (clean, or merged Release): execute the parked Put/Atomic on the current LLC copy, acknowledge -> I [D8].
- (TI_u, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/Execute/I`: ProbeAckData TtoN: save the dirty data, then execute the parked Put/Atomic on it and acknowledge -> I. S9.2-AA: an AccessAckData (Atomic) is issued only after the ProbeAckData is in.
- (TI_u, ProbeAck_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TI_u, ProbeAckData_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TI_u, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TI_u, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TI_u, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TI_u, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TI_u, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TI_u, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TI_u, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TI_u, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TI_u, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TI_u, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TI_u, Get_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, PutAtomic_u) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TI_u, Intent_CBOInval) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, Intent_CBOClean) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].
- (TI_u, Intent_CBOFlush) -> `Stall/`: Stall: an uncached access is being served on the block (Probes out or outer access outstanding); the request is re-decoded when the block is stable again [S6.5: the uncached access is acknowledged before any later transfer].

#### Row BN_i

- (BN_i, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BN_i, last_ProbeAck_toN) -> `RemoveHolder/DecCount/Drop/HintAck/N`: Every Branch is invalidated: drop the LLC copy (no write-back, dirty data discarded) and HintAck -> N [S8.2.3, plan S3.5].
- (BN_i, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BN_i, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_i, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_i, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BN_i, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted.
- (BN_i, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. The counted ProbeAck is still awaited.
- (BN_i, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_i, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_i, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BN_i, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_i, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_i, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_i, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_i, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BN_i, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_i, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row TN_i

- (TN_i, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TN_i, last_ProbeAck_toN) -> `RemoveHolder/DecCount/Drop/HintAck/N`: The Tip is gone: drop the LLC copy, HintAck -> N [plan S3.5].
- (TN_i, ProbeAckData_toN) -> `RemoveHolder/DecCount/Drop/HintAck/N`: ProbeAckData TtoN: the dirty data is discarded (CBOInval), the LLC copy dropped, HintAck -> N [plan S3.5 'dirty data discarded'].
- (TN_i, ProbeAck_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_i, ProbeAckData_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_i, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TN_i, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_i, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_i, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TN_i, ReleaseData_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip writes back and releases during a CBOInval (Figure 30); the data is not saved because the operation discards dirty data [plan S3.5]; ReleaseAck; the ProbeAck NtoN is still awaited.
- (TN_i, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TN_i, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_i, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_i, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_i, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_i, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TN_i, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_i, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row TB_c

- (TB_c, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TB_c, last_ProbeAck_toN) -> `RemoveHolder/DecCount/PutFullData_out/I_w`: The owner left instead of downgrading (or released first): no inner copy; write the LLC copy outward, HintAck after the AccessAck -> I_w.
- (TB_c, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/PutFullData_out/I_w`: The owner wrote back and left: save, write outward -> I_w.
- (TB_c, ProbeAck_toB) -> `OwnerToSharer/DecCount/PutFullData_out/B_w`: The clean owner is now a Branch; the LLC copy (possibly dirty from an earlier write-back) is written outward; copies kept at Branch -> B_w [plan S3.5 CBOClean].
- (TB_c, ProbeAckData_toB) -> `SaveData/OwnerToSharer/DecCount/PutFullData_out/B_w`: The dirty owner wrote back and kept a Branch: save, write outward -> B_w.
- (TB_c, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TB_c, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_c, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TB_c, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TB_c, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TB_c, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TB_c, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_c, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_c, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_c, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TB_c, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TB_c, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TB_c, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row B_w

- (B_w, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_w, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_w, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_w, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_w, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (B_w, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (B_w, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: A Branch releases during the clean's write-back: nothing depends on it (no Probe outstanding); accept and acknowledge, stay.
- (B_w, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/I_w`: The last Branch releases during the write-back: no inner copy remains -> I_w (same completion, HintAck -> I).
- (B_w, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (B_w, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (B_w, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (B_w, AccessAckData) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (B_w, AccessAckData_denied) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (B_w, AccessAck) -> `HintAck/B`: The outer write is acknowledged: the block is clean in memory, copies kept at Branch; HintAck carries completion [S8.2.3] -> B.
- (B_w, AccessAck_denied) -> `HintAck_denied/B`: The outer write was denied: nothing was written; the HintAck is denied [plan S3.5]; copies unchanged -> B.
- (B_w, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (B_w, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (B_w, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row I_w

- (I_w, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I_w, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I_w, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I_w, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I_w, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (I_w, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (I_w, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (I_w, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (I_w, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (I_w, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (I_w, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (I_w, AccessAckData) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (I_w, AccessAckData_denied) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (I_w, AccessAck) -> `HintAck/I`: The outer write is acknowledged: HintAck -> I (copy kept, now clean) [S8.2.3].
- (I_w, AccessAck_denied) -> `HintAck_denied/I`: The outer write was denied: HintAck denied, copy kept -> I.
- (I_w, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (I_w, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (I_w, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row BN_f

- (BN_f, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, ProbeAck_toN) -> `RemoveHolder/DecCount/`: RemoveHolder/DecCount, stay: a Branch answered BtoN (or NtoN after a merged Release) and others are still probed; remove it from the directory, count down [S9.3.5].
- (BN_f, last_ProbeAck_toN) -> `RemoveHolder/DecCount/PutFullData_out/N_w`: Every Branch is invalidated: write the LLC copy outward; drop it and HintAck when the AccessAck arrives -> N_w.
- (BN_f, ProbeAckData_toN) -> `Fault/`: Fault: only Branches were probed and a Branch never holds dirty data (NtoN/BtoN carry no data; ProbeAckData BtoN would be a master holding dirty data at Branch, which Table 29 forbids).
- (BN_f, ProbeAck_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_f, ProbeAckData_toB) -> `Fault/`: Fault: the Probe was toN; a ProbeAck TtoB / BtoB does not honour the cap [S9.3.3 'cap their permissions'].
- (BN_f, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (BN_f, Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted.
- (BN_f, last_Release_BtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: a Branch releases voluntarily while its Probe toN is outstanding (Figure 30). The manager must accept the Release and acknowledge it, because the master holds its ProbeAck until the ReleaseAck [S9.2-R]; the probe count is unchanged: the ProbeAck (NtoN, decoded as ProbeAck_toN) still arrives and is still counted. The counted ProbeAck is still awaited.
- (BN_f, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_f, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (BN_f, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (BN_f, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_f, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_f, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_f, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (BN_f, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (BN_f, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (BN_f, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row TN_f

- (TN_f, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, ProbeAck_toN) -> `Fault/`: Fault: exactly one master was probed, so the decoder always yields the last_ variant; a non-last ProbeAck_toN here is a count error.
- (TN_f, last_ProbeAck_toN) -> `RemoveHolder/DecCount/PutFullData_out/N_w`: The Tip is gone (clean or merged Release): write the LLC copy outward -> N_w.
- (TN_f, ProbeAckData_toN) -> `SaveData/RemoveHolder/DecCount/PutFullData_out/N_w`: ProbeAckData TtoN: save the dirty data, write it outward -> N_w.
- (TN_f, ProbeAck_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_f, ProbeAckData_toB) -> `Fault/`: Fault: the owner was probed toN; TtoB does not honour the cap [S9.3.3].
- (TN_f, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (TN_f, Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_f, last_Release_BtoN) -> `Fault/`: Fault: a Tip has no sibling Branches at this level, so no Release BtoN can arrive (M-PERM).
- (TN_f, Release_TtoN) -> `RemoveHolder/ReleaseAck/`: RemoveHolder/ReleaseAck, stay: the Tip releases (clean) while its Probe is outstanding (Figure 30); accept and acknowledge, keep waiting for its ProbeAck NtoN (count unchanged).
- (TN_f, ReleaseData_TtoN) -> `SaveData/RemoveHolder/ReleaseAck/`: SaveData/RemoveHolder/ReleaseAck, stay: the Tip writes back and releases while its Probe is outstanding (Figure 30); the data is saved (it is the current value), the Release acknowledged, and the ProbeAck NtoN is still awaited (count unchanged). 'The voluntary Release's data can be used to respond to the original requestor' [S9.2].
- (TN_f, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (TN_f, AccessAckData) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_f, AccessAckData_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_f, AccessAck) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_f, AccessAck_denied) -> `Fault/`: Fault: no outer request (Get, PutFullData, forwarded access) issued by a table cell is outstanding on the block. An eviction's PutFullData is answered to the port, not to the table (see the decode rules).
- (TN_f, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (TN_f, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (TN_f, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.

#### Row N_w

- (N_w, AcquireBlock_NtoB) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, AcquireBlock_NtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, AcquireBlock_BtoT) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, AcquireBlock_BtoT_last) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, AcquireBlock_BtoT_nonholder) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_w, last_ProbeAck_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_w, ProbeAckData_toN) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_w, ProbeAck_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_w, ProbeAckData_toB) -> `Fault/`: Fault: no Probe is outstanding on the block in this state (a Grant is issued only after every ProbeAck arrived [S9.2-G] and the stable states have none), so no ProbeAck can arrive.
- (N_w, GrantAck) -> `Fault/`: Fault: no Grant is unanswered on the block (GrantAck answers a Grant[Data], Table 30).
- (N_w, Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_w, last_Release_BtoN) -> `Fault/`: Fault: the directory lists no Branch for this block, so no master can legally send Release BtoN (M-PERM: a Release param starts from the tracked permission).
- (N_w, Release_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_w, ReleaseData_TtoN) -> `Fault/`: Fault: the directory lists no Tip for this block, so no master can legally send Release TtoN / ReleaseData TtoN (M-PERM).
- (N_w, Replacement) -> `Stall/`: Stall: the victim's block is busy; the controller retries the Replacement (as every transient state of MESI_LLC_directory_v2 + Replacement = Stall).
- (N_w, AccessAckData) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (N_w, AccessAckData_denied) -> `Fault/`: Fault: a PutFullData is answered by AccessAck only (Table 12).
- (N_w, AccessAck) -> `Drop/HintAck/N`: The flush's write is acknowledged: drop the LLC copy, HintAck -> N [S8.2.3: Flush = Clean then Inval].
- (N_w, AccessAck_denied) -> `HintAck_denied/I`: The outer write was denied: the data was not written, so the copy is kept (dropping it would lose it); HintAck denied -> I.
- (N_w, Get_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, PutAtomic_u) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, Intent_Prefetch) -> `HintAck/`: HintAck, stay: a prefetch hint may be ignored [S8, S8.2.2]; answering it at once in any state changes no permission and no data, so it needs no serialization.
- (N_w, Intent_CBOInval) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, Intent_CBOClean) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
- (N_w, Intent_CBOFlush) -> `Stall/`: Stall: a CMO is in progress on the block; plan S3.5: no Grant or Probe on the block from the Intent to the HintAck, serialized like a Grant->GrantAck window.
## 4. Invariants

1. **Single writer or multiple readers (CoherenceCheck).** At most one L1 is in E/M/IM_a-after-Grant
   per block, and then no L1 is in S/SM_a. The manager's view enforces it: a Grant toT is issued only in
   cells whose state lists no holder but the requester (I, NT_d after the fill, BT_p/BT_pu/TT_p after the
   last ProbeAck, B + BtoT_last), and a Grant toB only when no Tip is listed (B, TB_p after the owner's
   ProbeAck). The masters' view follows because a master changes permission only on a Grant it
   receives or a Probe it answers.
2. **Inclusion.** The LLC holds the line whenever a holder is listed: every cell that adds a holder
   starts from a state whose LLC copy exists (I, B, T, NT_d after `SaveData`), and the LLC frees the line
   (`Replacement` rows, `Drop`) only after every holder has answered a Probe toN (BN_e, TN_e, BN_i,
   TN_i, N_w) or from I.
3. **Manager blocked from Grant to GrantAck.** Every Grant action lands in T_g, B_g or N_g, whose rows
   Stall every Acquire, uncached access and CMO and send no Probe; the only exits are the GrantAck cells.
4. **Master blocked from Release to ReleaseAck.** The three `_r` states Stall every Probe and every
   access, and exit only on ReleaseAck.
5. **Master blocked from Acquire to Grant.** IS_a, IM_a and SM_a Stall every access and every
   Replacement (a Release is forbidden while a Grant is pending); they answer Probes (required) and exit
   only on a Grant event.
6. **One transaction per block at the manager.** Every transient manager state Stalls every new
   inner-A request; the manager serializes Acquires, uncached accesses and CMOs on a block in arrival
   order, which is the "trivially order the transactions" serialization of S9.2 at the granularity of a
   block.
7. **Probe count equals Probes sent.** The counter is set by the probe action and decremented by every
   ProbeAck cell; a merged Release never touches it, so the `last_` decode is exact even when the
   directory has already emptied (BT_p + last_Release_BtoN keeps waiting).

## 5. The races, as message sequence charts

Notation: `A`, `B` are L1 masters, `H` the manager; `(state)` after a message is the sender's state
once the cell ran; `X` is the block.

### 5.1 Figure 29: two Acquires, a Probe before the Grant

```
A: I  --Load-->  AcquireBlock NtoB  (IS_a)      ... delayed in the network
B: I  --Store--> AcquireBlock NtoT  (IM_a)
H: I  + AcquireBlock_NtoT(B)  -> SetOwner/GrantData_toT/T_g     (B's Acquire arrived first)
B: IM_a + GrantData_toT -> Hit/GrantAck/M
H: T_g + AcquireBlock_NtoB(A) -> Stall                           (blocked until B's GrantAck: step 5)
H: T_g + GrantAck(B) -> T
H: T + AcquireBlock_NtoB(A) -> ProbeBlock_toB_owner/TB_p        (A's parked Acquire re-decoded: step 6, 7)
B: M + ProbeBlock_toB -> ProbeAckData_TtoB/S
H: TB_p + ProbeAckData_toB -> SaveData/OwnerToSharer/DecCount/AddSharer/GrantData_toB/B_g
A: IS_a + GrantData_toB -> Hit/GrantAck/S
H: B_g + GrantAck(A) -> B      (sharers {A, B})
```

The step-8 obligation (the Grant to a master probed after its Acquire carries data) in the upgrade
variant:

```
A: S + Store -> AcquireBlock_BtoT (SM_a)                        ... delayed
B: S + Store -> AcquireBlock_BtoT (SM_a)                        ... arrives first
H: B{A,B} + AcquireBlock_BtoT(B) -> ProbeBlock_toN_all (to A only)/BT_pu
A: SM_a + ProbeBlock_toN -> ProbeAck_BtoN/IM_a                  (A answers although its Grant is pending)
H: BT_pu + last_ProbeAck_toN(A) -> RemoveHolder/DecCount/SetOwner/Grant_toT/T_g   (no data: B was never probed)
B: SM_a + Grant_toT -> Hit/GrantAck/M
H: T_g + AcquireBlock_BtoT_nonholder(A) -> Stall;  T_g + GrantAck(B) -> T   (A was removed by its ProbeAck BtoN)
H: T + AcquireBlock_BtoT_nonholder(A) -> ProbeBlock_toN_owner/TT_p    (A is not listed any more)
B: M + ProbeBlock_toN -> ProbeAckData_TtoN/I
H: TT_p + ProbeAckData_toN -> SaveData/RemoveHolder/DecCount/SetOwner/GrantData_toT/T_g   (data: step 8)
A: IM_a + GrantData_toT -> Hit/GrantAck/M      (Grant_toT without data would be a Fault in IM_a)
```

### 5.2 Figure 30: a voluntary Release meets a Probe

```
A: I + Load -> AcquireBlock_NtoB (IS_a)
B: M + Replacement -> ReleaseData_TtoN (MI_r)                   (at the same time)
H: T{B} + AcquireBlock_NtoB(A) -> ProbeBlock_toB_owner/TB_p
B: MI_r + ProbeBlock_toB -> Stall                               (held: no ProbeAck before the ReleaseAck)
H: TB_p + ReleaseData_TtoN(B) -> SaveData/RemoveHolder/ReleaseAck/   (accepted, acknowledged, still waiting)
B: MI_r + ReleaseAck -> I;  the parked Probe is re-decoded:  I + ProbeBlock_toB -> ProbeAck_NtoN/
H: TB_p + last_ProbeAck_toN(B, NtoN) -> RemoveHolder(no-op)/DecCount/AddSharer/GrantData_toB/B_g
A: IS_a + GrantData_toB -> Hit/GrantAck/S;  H: B_g + GrantAck -> B{A}
```

The Release's data answered the requester, as S9.2 says it may, and the Grant waited for the counted
ProbeAck. The Branch variant is BT_p (or BN_e, BI_u, BN_i, BN_f) + `Release_BtoN` =
`RemoveHolder/ReleaseAck/` with the counter untouched.

### 5.3 A Probe arriving in IS_a, IM_a or SM_a

- IS_a / IM_a: the master holds nothing; `ProbeAck_NtoN/`, state unchanged. The Probe was sent for a
  transaction serialized before this master's Acquire (the manager has not processed the Acquire, else
  the block would be blocked and no Probe sent). The later Grant is a GrantData in any case.
- SM_a + toN: `ProbeAck_BtoN/IM_a`. The Branch is pruned; the manager will see the requester as a
  non-holder when it processes the Acquire (`AcquireBlock_BtoT_nonholder`) and answer with GrantData
  (S5.1 second chart). SM_a + toB or toT: `ProbeAck_BtoB/`, nothing changes.

### 5.4 Replacement of a block with an Acquire outstanding

The L1 raises a Replacement on a victim in IS_a, IM_a or SM_a only if the data handler picks a transient
line; the cell is `Stall/` in all three: S9.2-R forbids a Release while a Grant is pending, and plan
S1.4 allows one Acquire per block, so the victim cannot be released and cannot be re-acquired. The
controller retries the Replacement after the Grant lands the line in S/E/M, where the ordinary Release
row runs. If the data handler never selects a transient line (the MSHR holds it, as today), the cells
are unreachable and harmless.

### 5.5 LLC eviction racing an inner Acquire

```
H: B{A} + Replacement -> ProbeBlock_toN_all/BN_e
A: S + Store -> AcquireBlock_BtoT (SM_a)                        (crosses the Probe)
A: SM_a + ProbeBlock_toN -> ProbeAck_BtoN/IM_a
H: BN_e + AcquireBlock_BtoT_last(A) -> Stall                    (A's Acquire arrives while A is still listed)
H: BN_e + last_ProbeAck_toN(A) -> RemoveHolder/DecCount/PutFullData_out/N
H: N + AcquireBlock_BtoT_nonholder(A) -> Get_out/NT_d           (the port's H1 holds the Get behind the write's AccessAck)
H: NT_d + AccessAckData -> SaveData/SetOwner/GrantData_toT/T_g;  A: IM_a + GrantData_toT -> Hit/GrantAck/M
```

A's Acquire is decoded as `AcquireBlock_BtoT_last` (or `AcquireBlock_BtoT`) while A is still listed
and as `AcquireBlock_BtoT_nonholder` once its ProbeAck removed it; all three are `Stall/` in BN_e, and
the parked message is re-decoded in N. Decode happens at pop time, never earlier, which is why a
Stall'ed Acquire can change event name while parked.

### 5.6 An uncached Put racing a Branch holder

```
U (uncached master): PutFullData X  -> H
A: S (Branch on X)
H: B{A} + PutAtomic_u -> ProbeBlock_toN_all/BI_u
A: S + ProbeBlock_toN -> ProbeAck_BtoN/I
H: BI_u + last_ProbeAck_toN -> RemoveHolder/DecCount/Execute/I    (write into the LLC copy, AccessAck to U)
A: I + Load -> AcquireBlock_NtoB;  H: I + AcquireBlock_NtoB -> SetOwner/GrantData_toT/T_g   (A reads the new value)
```

No holder can read a value older than the acknowledged Put (S6.5), because every holder lost its copy
before the AccessAck was sent (decision 8). If A's Load had hit in S before the Probe arrived, that
read is ordered before the Put, which is the legal serialization. An uncached Get instead probes only a
Tip toB (TB_u) and leaves Branches in place: their copies equal the LLC copy.

## 6. Parameters used on every message (the legal categories of plan S1.3)

| message | channel | category | values the tables use | never used |
|---|---|---|---|---|
| AcquireBlock | A | Grow | NtoB, NtoT, BtoT | |
| AcquirePerm | A | Grow | (not emitted, not decoded) | |
| ProbeBlock | B | Cap | toN, toB | toT (never sent; decoded at the L1 as a no-op Report) |
| ProbePerm | B | Cap | (not emitted) | |
| ProbeAck | C | Prune or Report | TtoB, TtoN, BtoN, TtoT, BtoB, NtoN | |
| ProbeAckData | C | Prune or Report without NtoN | TtoB, TtoN | BtoN, BtoB (no dirty Branch), TtoT (S9.2-PAD), NtoN (illegal) |
| Release | C | Prune or Report without NtoN | TtoN, BtoN | TtoB, BtoB, TtoT (no voluntary downgrade), NtoN (illegal) |
| ReleaseData | C | Prune or Report without NtoN | TtoN | TtoB, BtoB, TtoT, NtoN |
| Grant | D | Cap | toT | toB (a dataless Grant toB would be an upgrade to Branch, which never happens), toN (D7) |
| GrantData | D | Cap | toB, toT | toN (D7) |
| Grant, denied | D | Cap, ignored | toB or toT (= the requested cap) | toN (D7) |
| ReleaseAck | D | reserved 0 | 0 | |
| GrantAck | E | none | | |
| Get | outer A, inner A (uncached) | reserved 0 | 0 | |
| PutFullData, PutPartialData | outer A, inner A | reserved 0 | 0 | |
| ArithmeticData, LogicalData | inner A, outer A (forwarded) | Table 23 / 25 | as received | |
| Intent | inner A | Table 27 | 0, 1, 5, 6, 7 | 2-4 (unassigned: decode fault) |
| AccessAck, AccessAckData, HintAck | D | reserved 0 | 0 | |

## 7. Emissions per cell, for the admission contract of plan S5.2

Per cell that sends anything: the channels and counts the prediction step must reserve before the
message is popped. "holders" marks the fan-out of `ProbeBlock_toN_all`, counted from the directory at
prediction time (it cannot grow while the message waits, because the line is busy). Cells not listed
emit nothing. ID pools: every A emission needs a source from the master's A pool (the L1) or the LLC's
outer A pool; every C emission a source from the C pool (Release namespace for Release/ReleaseData, the
Probe's `source` copied for ProbeAck[Data], which needs no allocation, S6.4); every Grant a sink from
the manager's pool, released at the GrantAck.

### 7.1 L1 cells

| state | event | channel: emissions |
|---|---|---|
| I | Load | A: AcquireBlock_NtoB x1 |
| I | Store | A: AcquireBlock_NtoT x1 |
| I | ProbeBlock_toN | C: ProbeAck_NtoN x1 |
| I | ProbeBlock_toB | C: ProbeAck_NtoN x1 |
| I | ProbeBlock_toT | C: ProbeAck_NtoN x1 |
| IS_a | ProbeBlock_toN | C: ProbeAck_NtoN x1 |
| IS_a | ProbeBlock_toB | C: ProbeAck_NtoN x1 |
| IS_a | ProbeBlock_toT | C: ProbeAck_NtoN x1 |
| IS_a | GrantData_toB | E: GrantAck x1 |
| IS_a | GrantData_toT | E: GrantAck x1 |
| IS_a | Grant_denied | E: GrantAck x1 |
| IM_a | ProbeBlock_toN | C: ProbeAck_NtoN x1 |
| IM_a | ProbeBlock_toB | C: ProbeAck_NtoN x1 |
| IM_a | ProbeBlock_toT | C: ProbeAck_NtoN x1 |
| IM_a | GrantData_toT | E: GrantAck x1 |
| IM_a | Grant_denied | E: GrantAck x1 |
| SM_a | ProbeBlock_toN | C: ProbeAck_BtoN x1 |
| SM_a | ProbeBlock_toB | C: ProbeAck_BtoB x1 |
| SM_a | ProbeBlock_toT | C: ProbeAck_BtoB x1 |
| SM_a | Grant_toT | E: GrantAck x1 |
| SM_a | GrantData_toT | E: GrantAck x1 |
| SM_a | Grant_denied | E: GrantAck x1 |
| S | Store | A: AcquireBlock_BtoT x1 |
| S | Replacement | C: Release_BtoN x1 |
| S | ProbeBlock_toN | C: ProbeAck_BtoN x1 |
| S | ProbeBlock_toB | C: ProbeAck_BtoB x1 |
| S | ProbeBlock_toT | C: ProbeAck_BtoB x1 |
| E | Replacement | C: Release_TtoN x1 |
| E | ProbeBlock_toN | C: ProbeAck_TtoN x1 |
| E | ProbeBlock_toB | C: ProbeAck_TtoB x1 |
| E | ProbeBlock_toT | C: ProbeAck_TtoT x1 |
| M | Replacement | C: ReleaseData_TtoN x1 |
| M | ProbeBlock_toN | C: ProbeAckData_TtoN x1 |
| M | ProbeBlock_toB | C: ProbeAckData_TtoB x1 |
| M | ProbeBlock_toT | C: ProbeAck_TtoT x1 |

### 7.2 manager cells

| state | event | channel: emissions |
|---|---|---|
| N | AcquireBlock_NtoB | outer A: Get_out x1 |
| N | AcquireBlock_NtoT | outer A: Get_out x1 |
| N | AcquireBlock_BtoT_nonholder | outer A: Get_out x1 |
| N | Get_u | outer A: ForwardOut x1 |
| N | PutAtomic_u | outer A: ForwardOut x1 |
| N | Intent_Prefetch | inner D: HintAck x1 |
| N | Intent_CBOInval | inner D: HintAck x1 |
| N | Intent_CBOClean | inner D: HintAck x1 |
| N | Intent_CBOFlush | inner D: HintAck x1 |
| I | AcquireBlock_NtoB | inner D: GrantData_toT x1 |
| I | AcquireBlock_NtoT | inner D: GrantData_toT x1 |
| I | AcquireBlock_BtoT_nonholder | inner D: GrantData_toT x1 |
| I | Replacement | outer A: PutFullData_out x1 |
| I | Get_u | inner D: AccessAckData_in x1 |
| I | PutAtomic_u | inner D: Execute x1 |
| I | Intent_Prefetch | inner D: HintAck x1 |
| I | Intent_CBOInval | inner D: HintAck x1 |
| I | Intent_CBOClean | outer A: PutFullData_out x1 |
| I | Intent_CBOFlush | outer A: PutFullData_out x1 |
| B | AcquireBlock_NtoB | inner D: GrantData_toB x1 |
| B | AcquireBlock_NtoT | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | AcquireBlock_BtoT | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | AcquireBlock_BtoT_last | inner D: Grant_toT x1 |
| B | AcquireBlock_BtoT_nonholder | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | Release_BtoN | inner D: ReleaseAck x1 |
| B | last_Release_BtoN | inner D: ReleaseAck x1 |
| B | Replacement | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | Get_u | inner D: AccessAckData_in x1 |
| B | PutAtomic_u | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | Intent_Prefetch | inner D: HintAck x1 |
| B | Intent_CBOInval | inner B: ProbeBlock_toN_all x holders (fan-out) |
| B | Intent_CBOClean | outer A: PutFullData_out x1 |
| B | Intent_CBOFlush | inner B: ProbeBlock_toN_all x holders (fan-out) |
| T | AcquireBlock_NtoB | inner B: ProbeBlock_toB_owner x1 |
| T | AcquireBlock_NtoT | inner B: ProbeBlock_toN_owner x1 |
| T | AcquireBlock_BtoT_nonholder | inner B: ProbeBlock_toN_owner x1 |
| T | Release_TtoN | inner D: ReleaseAck x1 |
| T | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| T | Replacement | inner B: ProbeBlock_toN_owner x1 |
| T | Get_u | inner B: ProbeBlock_toB_owner x1 |
| T | PutAtomic_u | inner B: ProbeBlock_toN_owner x1 |
| T | Intent_Prefetch | inner D: HintAck x1 |
| T | Intent_CBOInval | inner B: ProbeBlock_toN_owner x1 |
| T | Intent_CBOClean | inner B: ProbeBlock_toB_owner x1 |
| T | Intent_CBOFlush | inner B: ProbeBlock_toN_owner x1 |
| NT_d | AccessAckData | inner D: GrantData_toT x1 |
| NT_d | AccessAckData_denied | inner D: Grant_denied x1 |
| NT_d | Intent_Prefetch | inner D: HintAck x1 |
| T_g | Intent_Prefetch | inner D: HintAck x1 |
| B_g | Intent_Prefetch | inner D: HintAck x1 |
| N_g | Intent_Prefetch | inner D: HintAck x1 |
| BT_p | last_ProbeAck_toN | inner D: GrantData_toT x1 |
| BT_p | Release_BtoN | inner D: ReleaseAck x1 |
| BT_p | last_Release_BtoN | inner D: ReleaseAck x1 |
| BT_p | Intent_Prefetch | inner D: HintAck x1 |
| BT_pu | last_ProbeAck_toN | inner D: Grant_toT x1 |
| BT_pu | Release_BtoN | inner D: ReleaseAck x1 |
| BT_pu | Intent_Prefetch | inner D: HintAck x1 |
| TB_p | last_ProbeAck_toN | inner D: GrantData_toB x1 |
| TB_p | ProbeAckData_toN | inner D: GrantData_toB x1 |
| TB_p | ProbeAck_toB | inner D: GrantData_toB x1 |
| TB_p | ProbeAckData_toB | inner D: GrantData_toB x1 |
| TB_p | Release_TtoN | inner D: ReleaseAck x1 |
| TB_p | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TB_p | Intent_Prefetch | inner D: HintAck x1 |
| TT_p | last_ProbeAck_toN | inner D: GrantData_toT x1 |
| TT_p | ProbeAckData_toN | inner D: GrantData_toT x1 |
| TT_p | Release_TtoN | inner D: ReleaseAck x1 |
| TT_p | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TT_p | Intent_Prefetch | inner D: HintAck x1 |
| BN_e | last_ProbeAck_toN | outer A: PutFullData_out x1 |
| BN_e | Release_BtoN | inner D: ReleaseAck x1 |
| BN_e | last_Release_BtoN | inner D: ReleaseAck x1 |
| BN_e | Intent_Prefetch | inner D: HintAck x1 |
| TN_e | last_ProbeAck_toN | outer A: PutFullData_out x1 |
| TN_e | ProbeAckData_toN | outer A: PutFullData_out x1 |
| TN_e | Release_TtoN | inner D: ReleaseAck x1 |
| TN_e | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TN_e | Intent_Prefetch | inner D: HintAck x1 |
| N_u | AccessAckData | inner D: ForwardIn x1 |
| N_u | AccessAckData_denied | inner D: ForwardIn x1 |
| N_u | AccessAck | inner D: ForwardIn x1 |
| N_u | AccessAck_denied | inner D: ForwardIn x1 |
| N_u | Intent_Prefetch | inner D: HintAck x1 |
| TB_u | last_ProbeAck_toN | inner D: AccessAckData_in x1 |
| TB_u | ProbeAckData_toN | inner D: AccessAckData_in x1 |
| TB_u | ProbeAck_toB | inner D: AccessAckData_in x1 |
| TB_u | ProbeAckData_toB | inner D: AccessAckData_in x1 |
| TB_u | Release_TtoN | inner D: ReleaseAck x1 |
| TB_u | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TB_u | Intent_Prefetch | inner D: HintAck x1 |
| BI_u | last_ProbeAck_toN | inner D: Execute x1 |
| BI_u | Release_BtoN | inner D: ReleaseAck x1 |
| BI_u | last_Release_BtoN | inner D: ReleaseAck x1 |
| BI_u | Intent_Prefetch | inner D: HintAck x1 |
| TI_u | last_ProbeAck_toN | inner D: Execute x1 |
| TI_u | ProbeAckData_toN | inner D: Execute x1 |
| TI_u | Release_TtoN | inner D: ReleaseAck x1 |
| TI_u | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TI_u | Intent_Prefetch | inner D: HintAck x1 |
| BN_i | last_ProbeAck_toN | inner D: HintAck x1 |
| BN_i | Release_BtoN | inner D: ReleaseAck x1 |
| BN_i | last_Release_BtoN | inner D: ReleaseAck x1 |
| BN_i | Intent_Prefetch | inner D: HintAck x1 |
| TN_i | last_ProbeAck_toN | inner D: HintAck x1 |
| TN_i | ProbeAckData_toN | inner D: HintAck x1 |
| TN_i | Release_TtoN | inner D: ReleaseAck x1 |
| TN_i | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TN_i | Intent_Prefetch | inner D: HintAck x1 |
| TB_c | last_ProbeAck_toN | outer A: PutFullData_out x1 |
| TB_c | ProbeAckData_toN | outer A: PutFullData_out x1 |
| TB_c | ProbeAck_toB | outer A: PutFullData_out x1 |
| TB_c | ProbeAckData_toB | outer A: PutFullData_out x1 |
| TB_c | Release_TtoN | inner D: ReleaseAck x1 |
| TB_c | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TB_c | Intent_Prefetch | inner D: HintAck x1 |
| B_w | Release_BtoN | inner D: ReleaseAck x1 |
| B_w | last_Release_BtoN | inner D: ReleaseAck x1 |
| B_w | AccessAck | inner D: HintAck x1 |
| B_w | AccessAck_denied | inner D: HintAck_denied x1 |
| B_w | Intent_Prefetch | inner D: HintAck x1 |
| I_w | AccessAck | inner D: HintAck x1 |
| I_w | AccessAck_denied | inner D: HintAck_denied x1 |
| I_w | Intent_Prefetch | inner D: HintAck x1 |
| BN_f | last_ProbeAck_toN | outer A: PutFullData_out x1 |
| BN_f | Release_BtoN | inner D: ReleaseAck x1 |
| BN_f | last_Release_BtoN | inner D: ReleaseAck x1 |
| BN_f | Intent_Prefetch | inner D: HintAck x1 |
| TN_f | last_ProbeAck_toN | outer A: PutFullData_out x1 |
| TN_f | ProbeAckData_toN | outer A: PutFullData_out x1 |
| TN_f | Release_TtoN | inner D: ReleaseAck x1 |
| TN_f | ReleaseData_TtoN | inner D: ReleaseAck x1 |
| TN_f | Intent_Prefetch | inner D: HintAck x1 |
| N_w | AccessAck | inner D: HintAck x1 |
| N_w | AccessAck_denied | inner D: HintAck_denied x1 |
| N_w | Intent_Prefetch | inner D: HintAck x1 |

## 8. Mapping to Octopus message kinds (plan S5.1), per action

| action (table) | TileLink message | Octopus kind | `Message::data` |
|---|---|---|---|
| L1 AcquireBlock_NtoB | AcquireBlock NtoB | `K_GETS` | no |
| L1 AcquireBlock_NtoT, AcquireBlock_BtoT | AcquireBlock NtoT / BtoT | `K_GETM` | no |
| L1 GrantAck | GrantAck | `K_GRANT_ACK` (new) | no |
| L1 ProbeAck_BtoN, ProbeAck_NtoN, ProbeAck_TtoN, ProbeAck_TtoB, ProbeAck_TtoT, ProbeAck_BtoB | ProbeAck | `K_ACK` | no |
| L1 ProbeAckData_TtoB, ProbeAckData_TtoN | ProbeAckData | `K_WB_DATA` (the plan's row "owner's data on a forward"; it is a supply that travels to the manager, so `K_WB_DATA` rather than `K_SUPPLY` keeps the LatencyReport's Resp-Bus stage for the Grant only) | yes |
| L1 Release_BtoN | Release BtoN | `K_PUTS` | no |
| L1 Release_TtoN | Release TtoN | `K_PUTE` | no |
| L1 ReleaseData_TtoN | ReleaseData TtoN | `K_PUTM` | yes |
| L1 Hit, Error | CPU response | `K_RESP` at retire (as today; Error additionally marks the response as denied) | |
| LLC ProbeBlock_toB_owner | ProbeBlock toB | `K_FWD` (Fwd-GetS) | no |
| LLC ProbeBlock_toN_owner | ProbeBlock toN to the Tip | `K_FWD` (Fwd-GetM) when serving an Acquire or an uncached access; `K_INV` when serving a Replacement or a CMO (the kind follows the consumed message, as the plan's Inv/InvM row says) | no |
| LLC ProbeBlock_toN_all | ProbeBlock toN to Branches | `K_INV` | no |
| LLC GrantData_toB, GrantData_toT, Grant_toT, Grant_denied | Grant[Data] | `K_RESP` | yes / no |
| LLC ReleaseAck | ReleaseAck | `K_ACK` (PutAck) | no |
| LLC Get_out | Get | `K_MEM_READ` | no |
| LLC PutFullData_out | PutFullData | `K_MEM_WRITE` | yes |
| LLC ForwardOut | the uncached access | `K_MEM_READ` (Get, Atomic) / `K_MEM_WRITE` (Put) | per opcode |
| LLC ForwardIn, AccessAckData_in, Execute | AccessAck[Data] | `K_RESP` | per opcode |
| LLC HintAck, HintAck_denied | HintAck | `K_ACK` | no |
| LLC consumed AccessAckData | fill | `K_FILL` | yes |
| LLC consumed AccessAck | write acknowledgement | `K_MEM_WACK` | no |

`K_GRANT_ACK` is the one new kind. Octopus never produces Get_u, PutAtomic_u or Intent on the inner A
(the CPU model issues Loads and Stores through the L1), so the uncached and CMO rows are exercised by
`tlsim` only (TL-UNCACHED, TL-CMO).

## 9. Deliberately not supported

| item | why | what happens |
|---|---|---|
| ProbeAckData TtoT | legal only with per-address ordered C (S9.2-PAD); the Network and the crossbar do not order C per address and the plan relies on that freedom (plan S5.3 I-C, `requires_ordering none`). M + ProbeBlock_toT answers ProbeAck TtoT without data | never emitted; decode fault at the manager |
| AcquirePerm | Octopus traces are word Loads and Stores, never a whole-block write (plan S5.1); supporting it needs a per-block "data not valid" flag on every T-family state of the manager (S9.3.2), which doubles those rows | decode fault at the manager; see S11 for the plan consequence |
| ProbePerm | never needed: a ProbeBlock asks for the data if the master has it dirty, and the manager always wants it (inclusive, it must hold the current bytes when it Grants) | never emitted; decode fault at the L1 |
| Voluntary downgrade (Release TtoB, Report Releases) | the L1 evicts whole lines; a downgrade-without-eviction has no Octopus event | decode fault at the manager |
| MOESI | O (dirty, shared) cannot exist among sibling L1s: a Branch is read-only and a probed dirty Tip must hand the manager the data (ProbeAckData TtoB), so the manager, not an L1, becomes the dirty owner (plan S5.1, decision 4) | the LLC copy after a ProbeAckData TtoB is exactly the "O at the manager"; it is written back on eviction |
| Grant[Data] toN | decision 7 | decode fault at the L1, M-PARAM violation |
| Update-based B/C Access and Hint messages (S9.5) | plan S2 out of scope | decode fault at the L1 |
| Multi-level TL-C | plan S9 | the manager is the root: its outer link is TL-UH |
| Same-cycle responses | decision 2 | every cell's emissions leave in the next cycle at the earliest |

## 10. Checklist: the S9.2 limits, where each is enforced by construction

| limit (S9.2) | side | monitor rule | enforced by |
|---|---|---|---|
| no Acquire while a Grant[Data] or AccessAck[Data] is pending on the block | master | M-CONC | an Acquire is emitted only from I and S (and the two Store cells); IS_a/IM_a/SM_a Stall Load and Store; the L1 issues no uncached access, so no AccessAck is ever pending |
| after an Acquire: no Acquire, Get, Put, Atomic until the Grant | master | M-CONC | the same three states Stall every access |
| no Release while a Grant[Data] or AccessAck[Data] is pending | master | M-CONC | IS_a/IM_a/SM_a + Replacement = Stall |
| after a Release: no ProbeAck, Acquire, Get, Put, Atomic, Release until the ReleaseAck | master | M-CONC | SI_r/EI_r/MI_r Stall every Probe and access and Fault a second Replacement; the only exit is ReleaseAck |
| the master still answers a Probe while its Grant is pending | master | Figure 29 | IS_a/IM_a answer NtoN, SM_a answers BtoN/BtoB; no Stall on a Probe outside the `_r` states |
| no Grant[Data] while a ProbeAck[Data] is pending | slave | M-CONC | every Grant action sits in a `last_ProbeAck*` / `ProbeAck*Data` cell of a probing state (counter reaches 0 in that cell) or in a state with no Probe outstanding (I, B, NT_d) |
| after a Grant[Data]: no Probe until the GrantAck | slave | M-CONC | T_g/B_g/N_g emit no Probe (every inner request Stalls) and exit only on GrantAck |
| no Probe while a GrantAck is pending | slave | M-CONC | same states |
| at most one Probe outstanding per master and block | slave | M-CONC | a probing state sends its Probes in the entering cell only and Stalls every request until the counter is 0; Replacement and CMOs Stall there too |
| no AccessAckData while a ProbeAckData is pending | slave | M-CONC | AccessAckData_in / Execute / ForwardIn run only in a `ProbeAck*` completion cell of TB_u/BI_u/TI_u (the ProbeAckData has arrived) or in I/B/N_u where no Probe is outstanding |
| ProbeAckData TtoT only with ordered C | master | M-PARAM | never emitted |
| Acquire param starts from the tracked permission | master | M-PERM | NtoB/NtoT only from I, BtoT only from S |
| ProbeAck/Release param starts from the tracked permission | master | M-PERM | the param of every ProbeAck*/Release* action names the state it is emitted from (I: NtoN; S/SM_a: Bto*; E/M: Tto*) |
| Grant cap meets the request; denied leaves permissions unchanged | slave | M-PERM, M-PARAM | NtoB -> GrantData toB or toT; NtoT -> GrantData toT; BtoT -> Grant toT (unprobed requester) or GrantData toT; Grant_denied changes no holder set and the L1 keeps its state (I or S) |
| Figure 30: accept a Release while probing, Grant only after every ProbeAck | slave | TL-FLOWS | every probing state has `RemoveHolder/ReleaseAck/` (plus `SaveData`) for the probed holder's Release and keeps the counter |

## 11. Open questions and where the tables depart from the plan

1. **AcquirePerm and the data-not-valid flag (plan S3.5 `CoherentManager`, S6 TL-PERM).** The task of
   T5 lists AcquirePerm as not supported, so the manager table decodes it as a fault and TL-PERM cannot
   pass against these tables. Supporting it later is a flag outside the FSM state (`data_not_valid` per
   block, set by a Grant to an AcquirePerm, cleared by `SaveData` from ReleaseData/ProbeAckData or by a
   full-block Put) plus three extra checks: a ProbeAck without data from the Tip while the flag is set is
   a fault, a Release TtoN without data while set is a fault, and `AccessAckData_in` in T is replaced by
   a probe (already the case: T + Get_u probes). The plan should say which step owns this; T6 as
   written assumes it exists.
2. **CBOClean in B does not probe (plan S3.5 says "ProbeBlock toB to every holder").** A Branch holds a
   clean read-only copy identical to the LLC's; a toB Probe to it is a no-op by S9.3.3 and only costs a
   round trip per holder. The table writes the LLC copy outward and keeps the Branches. The plan's
   wording should be narrowed to "to the Tip, if any".
3. **Uncached accesses in N are forwarded, not allocated.** Plan S3.5 does not say; the table forwards
   (`ForwardOut`) because an uncached access creates no inner copy and inclusion has nothing to say
   about it. An LLC that allocates on uncached reads would be a one-cell change (N + Get_u -> Get_out
   into a fill state) and is left out to keep the data array semantics of Octopus.
4. **Grant param on a denied Grant.** D7 says "its param has no semantic meaning but may not encode toN";
   the table fixes it to the requested cap so that the monitor's "meets or exceeds" check can run on
   every Grant uniformly. The plan may want to record that choice.
5. **A Release that overtakes its own GrantAck.** C and E are not ordered relative to each other, so a
   master's Release can reach the manager while the manager is still in T_g/B_g. The table Stalls it
   (served after the GrantAck). The plan's S5.2 admission contract must therefore let a Stall'ed C
   message sit in the queue without blocking the E queue, which its per-channel queues do; a
   single-queue port would deadlock here. Worth a TL-NEG case.
6. **Replacement of a transient L1 line** (S5.4) is `Stall` on the assumption that the controller
   retries; if `checkReplacements` can pick a transient victim, the retry must be guaranteed by the
   controller (as today for the directory tables).
7. **toB instead of toT in TB_p after the owner vanished.** When the probed Tip answers TtoN/NtoN the
   requester could be granted toT (no copy remains). The table grants toB so that the Grant type
   depends only on the request and the stable state the Acquire was decoded in; it costs one later
   BtoT upgrade if that reader writes. A policy knob, not a correctness issue.
8. **MSI preset.** Settled in T6: `TLC_MSI_LLC.csv`, two cells and one appended state (S3.1). The L1
   file is shared.

### 11.1 Found in Octopus (T7, 2026-10-22)

9. **L1 (I, Replacement) = Fault assumes the victim is still valid.** In Octopus a fill that needs a way
   moves the victim to the write-back buffer and raises the Replacement as a separate, later event
   (`CacheController::checkReplacements`); a ProbeBlock toN can take the line in between, and the
   Replacement then finds it in I (seen on 2 banks, a2time01). The TLC_L1 handler treats a Replacement
   whose line is no longer in the write-back buffer as a no-op, the same rule CacheMaster applies to an
   Evict of an absent line. The cell is right for a synchronous replacement; the race belongs in S5.4.
10. **Engine rule 3 (S1.2) and S7 disagree on the parked answer's reservation.** Rule 3 keeps the parking
    cell's reservation until the answering cell; S7 lists the answer's inner D on the answering cell.
    Octopus follows S7: holding a D slot across parking can deadlock, because a refused non-demand
    message blocks the queue behind it for that cycle (a Release waiting for a D slot held by a parked
    transaction whose last ProbeAck is queued behind that Release). With S7 every wait is on a queue or
    an id pool that drains by itself. S7 is the rule.
11. **The B transmit depth must be at least the largest probe fan-out** (the number of L1s, for an LLC
    eviction of a block every L1 holds), or the probing message can never be admitted. The 4-core
    defaults satisfy it; 8 cores need `tx_depth_B >= 8`. TileLinkInterconnect does not check it yet.
