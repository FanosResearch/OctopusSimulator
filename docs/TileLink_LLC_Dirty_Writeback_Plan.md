# TL-C LLC: write back only dirty blocks (plan)

Status: plan approved 2026-10-26 (bit in Octopus's main cache line; protocol-by-protocol maintenance later). Branch: `tilelink`.

## 1. The problem

The TL-C manager tables (`TileLink/tables/TLC_MESI_LLC.csv`, `TLC_MSI_LLC.csv`) write every evicted block
back to memory, clean or dirty. State `I` (no client holds the block, the LLC's copy is valid) has no dirty
variant, and its eviction cell is unconditional:

| Cell | Action |
|---|---|
| `(I, Replacement)` | `PutFullData_out/N` |
| `(BN_e, last_ProbeAck_toN)` | `RemoveHolder/DecCount/PutFullData_out/N` |
| `(TN_e, last_ProbeAck_toN)` | `RemoveHolder/DecCount/PutFullData_out/N` |
| `(TN_e, ProbeAckData_toN)` | `SaveData/RemoveHolder/DecCount/PutFullData_out/N` |

A block filled from memory and only read is written back when it leaves the LLC. SiFive's InclusiveCache
(the RTL we compare with, `docs/TileLink_RTL_Validation_Plan.md`) keeps a dirty bit per block
(`meta.dirty`) and writes back only dirty blocks. The extra writes add memory traffic and memory-side
queueing to every eviction scenario, so Octopus and the RTL would differ there for a reason that is not
under study.

## 2. When the LLC's copy is dirty

Checked in the tables and code:

- **Becomes dirty:** `SaveData` of data that came from a client: `ReleaseData_TtoN` and
  `ProbeAckData_toN` or `_toB` (channel C). The L1 table sends data only from M
  (`(M, Replacement)` -> `ReleaseData_TtoN`; `(M, ProbeBlock_toN/toB)` -> `ProbeAckData_TtoN/TtoB`); E and S
  send dataless `Release` or `ProbeAck`. In the library (tlsim) also `Execute` (an atomic or uncached Put
  performed in the LLC); Octopus's LLC faults on `Execute`, since its L1 never issues uncached accesses.
- **Clean:** after a fill (`SaveData` of the outer `AccessAckData`, channel D), and after the block is
  written to memory (`PutFullData_out`, including the CMO cells, whose block stays resident).
- **Unchanged:** a dataless `Release` or `ProbeAck`. A block that was dirty in the LLC, granted to an L1
  in E and released clean, is still dirty.

## 3. Design

A dirty bit per cache line in Octopus's main cache-line model (decided 2026-10-26: not tied to TileLink),
as InclusiveCache keeps one, rather than doubling the TL-C states.

### 3.1 Octopus main code: the bit

1. **`GenericCacheLine::dirty`** (`header/GenericCacheLine.h`), false on construction and allocation.
   It is **not** part of `copyBits`: `updateLineBits` writes a protocol-built line through `copyBits`
   (`src/CacheDataHandler.cpp`:156) only to set a new state, and would reset it. It moves with the whole
   line: `copy`/`operator=` (MSHR to array in `writeCacheLine_bypassLatency`, array to write-back buffer),
   and `readLineBits` reports it.
2. **`CacheDataHandler` API**: `isDirty(addr)`, `setDirty(addr, bool)`, finding the line in the array, an
   MSHR or the write-back buffer (`CacheDataHandler_COTS`).
3. **Protocol-independent rules**, in the base controllers:
   - a CPU Store's bytes written into the line (`MODIFY_DATA`): dirty;
   - at the LLC (`m_log_role == Logger::Role::LLC`, set by both system builders; not "no CPU port":
     `m_cpu_port` is the ExternalCPU port, NULL for every trace-driven L1): data from memory, which every
     memory model tags `K_FILL` (`MainMemoryController`, `MCsimInterface`, the perfect-LLC loopback), is
     clean; any other data came from an L1 (a write-back: `PutM`, `ReleaseData`, `ProbeAckData`, a snoop or
     directory owner's data to the home) and is dirty. The protocols' own kind tags are not used (the
     directory protocols leave them untagged); the source interface is not used either (a directory LLC
     takes L1 and memory traffic on one interface);
   - the line's own copy written to the next level while the line stays (`performWriteBack` of this
     cache's copy, not a cache-to-cache supply): clean.
   Cache-to-cache transfers are protocol-dependent (an MSI GetM supplied by an M peer leaves the requester
   dirty; a GetS supplied the same way also updates the LLC, so the requester is clean). The snoop and
   directory protocols encode dirtiness in their states (M, O), and keep doing so: in their L1s the bit is
   documented as not maintained. A later step can set it from their states, checked by an assertion that
   "M or O" and "dirty" agree.
4. Nothing reads the bit until 3.3, so this step must leave every regression output byte-identical.

### 3.2 The tables and the policy engine

1. **A new table action `PutFullData_dirty_out`**: write the block to memory if it is dirty, otherwise
   emit nothing. Policy engine: `MgrOp::PutFullDataDirtyOut`, parsed in `TileLink/src/coh/Policy.cpp`,
   named in `mgrOpName`.
2. **New table files** (FSM variants never edit the current file): `TLC_MESI_LLC_v2.csv` and
   `TLC_MSI_LLC_v2.csv`. The only differences: the action appended as the last `ActionNum` (25), and the
   four cells of section 1 using it in place of `PutFullData_out`. No new states, so no row-order risk.
   The CMO cells (`Intent_CBOClean/Flush` in `I` and `B`, `TB_c`, `BN_f`, `TN_f`) are unchanged: they wait
   in `_w` states for the memory's AccessAck before answering, so skipping the write would need different
   next states; Octopus's L1 never issues CMOs, and InclusiveCache (TileLink 1.8.1) has none.
3. **The standalone library** (tlsim) has no Octopus dependency, so its `CoherentManager::Entry` gets its
   own dirty flag with the rules of section 2 (plus `Execute`), and executes the action.

### 3.3 Octopus's TL-C LLC uses the bit

`TLC_LLC.cpp` executes `PutFullData_dirty_out` by reading `isDirty` from its data handler: when dirty,
exactly what `PutFullData_out` does today; when clean, nothing (no memory write, and no write-back buffer
entry left behind: to be checked in the fill-first replacement path, `CacheController.cpp`:867-893).
Counters: `writebacks` only when sent, a new `clean_evictions`. `TLC_LLC::emittedBy`: the action may emit
one `K_MEM_WRITE` (the admission predictor treats it as possible). The SaveData rules need no TL-C code:
`ReleaseData` and `ProbeAckData` arrive from the level above (dirty), the fill from memory (clean).

**Checkers.** ValueCheck reads memory's value after a clean eviction: if the bit ever missed a write, a
later read returns stale data and ValueCheck fails, so the checker guards the change.

Existing presets keep the v1 tables (regression references unchanged). The Chipyard-matching preset uses
v2. Switching the default presets to v2 is a separate decision, with measured differences shown first.

## 4. Tests

| Id | Test | Expected |
|---|---|---|
| DW-1 | tlsim unit: fill, read, evict | no outer write |
| DW-2 | tlsim unit: ReleaseData, evict | one outer write with the released bytes |
| DW-3 | tlsim unit: ProbeAckData (owner probed for another client), evict | one write |
| DW-4 | tlsim unit: dirty in LLC, GrantData toT, clean Release, evict | one write (still dirty) |
| DW-5 | tlsim unit: fill, uncached Put / atomic (`Execute`), evict | one write |
| DW-6 | tlsim shipped systems with v2 tables | ValueCheck, CoherenceCheck, GraphCheck clean |
| DW-7 | Octopus unit: the bit across allocate, Store, `updateLineBits`, MSHR to array, array to write-back buffer, `readLineBits` | set and kept as in 3.1 |
| DW-8 | `run.sh reg` (all suites) after 3.1 alone | byte-identical to the references |
| DW-9 | Octopus REG-TILELINK suite with v2 tables swapped in | no faults; checkers clean; memory writes v2 <= v1, and v2 = the evictions of dirty blocks (from the counters) |
| DW-10 | Octopus EEMBC (and SPLASH) over TileLink, MSI and MESI v2 | complete, checkers clean; memory-write and latency differences reported against v1 |
| DW-11 | `run.sh reg` with the default (v1) presets after 3.3 | unchanged references |

## 5. Docs

`docs/TileLink_TLC_Tables.md`: the v2 tables, the dirty rule (section 2), and the correction of its
statement (line 454) that the data handler decides whether a clean line is written back.
`docs/TileLink_Guide.md`: the v2 tables in sections 7 and 16.

## 6. Steps

1. The library: policy-engine action, `CoherentManager` dirty flag, v2 tables; DW-1 to DW-6.
2. Octopus main code: the bit, its API and the protocol-independent rules (3.1); DW-7, DW-8.
3. Octopus's TL-C LLC uses it (3.3); DW-9 to DW-11.
4. Docs.

## 7. Progress

### Step 1, the library (2026-10-26): done

- `MgrOp::PutFullDataDirtyOut` ("PutFullData_dirty_out"): `TileLink/include/tilelink/coh/Policy.h`,
  `TileLink/src/coh/Policy.cpp`. `CoherentManager::Entry::dirty`, set by `SaveData` of channel-C data and by
  `Execute`, cleared by `SaveData` of the fill (channel D), by `Drop` and by any write to memory; the action
  writes only when dirty, else counts `clean_evictions` (`TileLink/src/agents/CoherentManager.cpp`).
- `TileLink/tables/TLC_MESI_LLC_v2.csv`, `TLC_MSI_LLC_v2.csv`: generated from v1, action 25 appended, the four
  cells changed; a cell-by-cell diff shows nothing else.
- Octopus's `TLC_LLC` faults on the new action until step 3 (so a v2 table cannot silently drop writes).
- DW-1 to DW-5: `TileLink/test/dirty/test_dirty.cpp` (TL-DIRTY), MESI and MSI, 82 checks, all pass. The whole
  library suite: 47/47 tests pass.
- DW-6: `tlsim` system 3 (4 CacheMasters with random loads and stores, an 8-set 2-way manager), MESI and MSI,
  v1 and v2, seeds 1 to 5, CoherenceCheck (C-SWMR, C-GRANT, C-VALUE: a missed write-back would return a stale
  value after the refill) and the monitors: all 20 runs complete with exit 0. ValueCheck watches uncached
  traffic only, and saw none here. Memory link, seed 1: v1 MESI 14,600 PutFullData for 14,616 Gets (every
  filled block written back); v2 MESI 7,098 for 14,804 (MSI: 14,705 / 14,721 against 7,021 / 14,696). A-channel
  beats on the memory link fall from about 131,500 to 70,000 in every seed; run lengths change by under 0.4%
  (this workload is bound by probes and grants, not memory).

### Step 2, Octopus main code (2026-10-26): done

- `GenericCacheLine::dirty` (`header/GenericCacheLine.h`, `src/GenericCacheLine.cpp`): false on construction,
  copied by `copy`/`operator=`, not by `copyBits`. `CacheDataHandler::isDirty`/`setDirty` (array, MSHR or
  write-back buffer, through `findline`/`getLine`); `readLineBits` reports the bit.
- Rules: `CacheController::modifyData` (a CPU Store's bytes) sets it; `CacheController::writeCacheLineData` at the
  LLC (`m_log_role == Logger::Role::LLC`) clears it for `K_FILL`/`K_FILL_ROLLBACK` and sets it for any other data;
  `BaseController::performWriteBack` clears it when the cache's own copy goes to the next level (not on a
  cache-to-cache supply, not on a perfect-LLC dropped write). The two `writeCacheLine_bypassLatency` callers
  (perfect-LLC pre-warm, `initialize`) insert absent lines with the initial image: clean, correctly.
- DW-7: `test/network/unit/dirty_bit.cpp` (DIRTY-BIT-LOAD-STORE, DIRTY-BIT-MOVES), directory MSI and MESI,
  occupancy 0 and 4, pipelined 4: a Load's fill clean; a Store dirty; `readLineBits` reports it;
  `updateLineBits` keeps it; set in the MSHR it is in the array after the fill; an evicted stored line is dirty
  in the write-back buffer (observed 1 cycle in 4 of 6 configurations; 0 cycles when pipelined) and never dirty
  after its write-back. Both pass. (The controller is not deleted in the test: `~BaseController` crashes
  outside a system, a pre-existing limitation that `data_store.cpp` also avoids.)
- DW-8: `test/network/run.sh all`: 145 PASS, 9 XFAIL (environment: ASan, cross-platform, BookSim, viewer and
  similar), 0 FAIL; REG-BUS, REG-ENV, REG-MESH, REG-TDM, REG-HAZARD, REG-MEMNET, REG-TILELINK identical to their
  references. `test/tilelink/run.sh all`: 56 PASS. `Octopus_tests` 65/65, `Octopus_tl_tests` 41/41.

### Step 3, Octopus's TL-C LLC (2026-10-26)

- `TLC_LLC.cpp`: `PutFullData_dirty_out` writes when the line's bit is set or the cell's own `SaveData` holds an
  L1's data (installed after the cell), else counts `clean_evictions`; the summary adds a line
  "`TLC_LLC <id>: <n> clean evictions not written back`" only when n > 0 (the v1 output is unchanged).
  `emittedBy`: at most one `K_MEM_WRITE`.
- Cross-check, v1 and v2: `TLC_LLC::Dir::dirty_view`, the TL-C events' own view (an L1's saved data sets it, the
  fill clears it, a write clears it), is compared with the generic bit at every `PutFullData_out` and
  `PutFullData_dirty_out`; a disagreement is a fault. It never fired.
- DW-9/DW-10: `test/tilelink/dirty_wb.sh` (the oct-evict stress: 4 KiB LLC, 2 KiB per bank with two banks, one-entry
  write-back buffers, shallow ports; ten EEMBC benchmarks; every diagnostic on). 120 runs, all 10/10 per batch
  (4/4 cores, exit 0, no fault, 0 monitor violations, hazard 0/0, tiling ok); accounting holds (v1 replacements =
  write-backs = memory writes; v2 replacements = write-backs + clean evictions, write-backs = memory writes).

  | system | table | replacements | write-backs | clean | mean latency | max latency |
  |---|---|---|---|---|---|---|
  | 1 bank, MESI | v1 | 60,467 | 60,467 | 0 | 84.8 | 605 |
  | 1 bank, MESI | v2 | 60,349 | 19,801 | 40,548 | 84.4 | 708 |
  | 1 bank, MSI | v1 | 60,378 | 60,378 | 0 | 85.6 | 799 |
  | 1 bank, MSI | v2 | 60,478 | 19,856 | 40,622 | 84.7 | 818 |
  | 2 banks, MESI | v1 | 79,992 | 79,992 | 0 | 92.3 | 545 |
  | 2 banks, MESI | v2 | 79,584 | 27,328 | 52,256 | 92.3 | 642 |
  | 2 banks, MSI | v1 | 80,172 | 80,172 | 0 | 92.7 | 539 |
  | 2 banks, MSI | v2 | 79,622 | 27,402 | 52,220 | 92.6 | 659 |
  | memory over TileLink, MESI | v1 | 60,746 | 60,746 | 0 | 85.7 | 641 |
  | memory over TileLink, MESI | v2 | 60,273 | 19,775 | 40,498 | 85.3 | 707 |
  | memory over TileLink, MSI | v1 | 60,418 | 60,418 | 0 | 86.5 | 733 |
  | memory over TileLink, MSI | v2 | 60,030 | 19,692 | 40,338 | 86.0 | 841 |

  v2 writes back about a third of v1's evictions; mean latency changes by under 1.1%. **The maximum latency rises
  in every v2 batch (+19 to +120 cycles). Explained** (below).
- **Why the maxima rise.** The v2 worst request (1 bank, MESI, a2time01, core 1, request 116525, 708 cycles: L2 stall
  646) traced with `--trace` and `test/network/linetable.py` (block 0x7f79ea6d3cc0): four cores Load the block
  within 92 cycles (457,042 to 457,134). Core 0 misses (fill at 457,333), core 3 is served by a probe; in the cycle
  the block reaches B (457,451) the 4 KiB LLC evicts it (`B --Replacement--> BN_e`), and, clean, it is dropped with
  no write (457,501, `last_ProbeAck_toN --> N`). Core 2's parked GetS then misses again (fill at 457,759, 258
  cycles), and core 1 is served after it: two memory round trips. In v1 every evicted block, clean or not, waits in
  the write-back buffer until memory accepts its write, and a re-miss in that window is served from the in-flight
  write ("reads forwarded at the LLC"). On this block: v1, 98 re-misses within 100 cycles of an eviction, refilled in
  1 to 10 cycles; v2, 77 such re-misses, each refilled in 257 cycles (memory). Over the benchmarks, forwarded reads
  fall (a2time01 178 to 61, basefp01 51 to 12) and memory reads rise accordingly.
  **So v1's lower maxima came from writing back clean blocks: the write-back buffer acted as a victim buffer for
  every evicted block.** InclusiveCache drops a clean victim, so a re-miss goes to memory, as in v2. The v2 maxima
  are the realistic ones; v1 TL-C worst cases on eviction-heavy workloads are optimistic. (Whether the snoop and directory
  LLC tables write back clean blocks has not been checked.)
- DW-11 (after step 3, default v1 presets, the dirty cross-check active): `test/network/run.sh all` 145 PASS,
  9 XFAIL, 0 FAIL, the seven REG suites identical to their references; `test/tilelink/run.sh all` 56 PASS. The
  cross-check did not fire in any suite.
- Committed: tilelink bbff70ed (steps 1 to 3).

### Step 4, docs (2026-10-26): done

`docs/TileLink_TLC_Tables.md`: s3.5 (the v2 tables, the dirty rule, the CMO cells kept), the action in s3.3, the
x0..1 emission in s7, the kind in s8, and the corrected (I, Replacement) justification (it said the data handler
decides whether a clean line transfers; nothing did). `docs/TileLink_Guide.md` s7.1 and s16 (the guide is not yet
in the repository). Remaining: whether the default presets switch to v2 (a separate decision).
