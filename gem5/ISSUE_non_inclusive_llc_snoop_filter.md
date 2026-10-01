# LLC: non-inclusive data with a tracking directory (snoop filter) instead of back-invalidating the L1s on every LLC eviction

**Type:** feature / model fidelity
**Area:** LLC controller, LLC protocol tables, COTS data handler, gem5 preset
**Branch:** `gem5_integration`

## Summary

The LLC is strictly inclusive: every LLC replacement broadcasts an invalidation to
all L1s, so an L1 loses a line whenever the LLC's LRU, which only ever sees L1
misses, chooses it as a victim. In the gem5 preset (eight 512 KiB L1s under an
8 MiB LLC) this is the dominant source of L1 misses and the main reason the
Octopus arm trails gem5's classic caches on the ov2slam workload. Proposal:
separate *tracking* (which L1s hold a line) from *data* (what the LLC stores), so
that evicting LLC data never recalls a line from an L1, the way gem5 classic's
mostly-inclusive L2 and Ruby CHI's home node already work. A smaller first step,
presence-aware LLC replacement, is included as an option.

## Motivation, with numbers

ov2slam ROI, 4 x ARM O3 restored from the same checkpoint, identical cache sizes in
both arms (run dirs `~/gem5_arm/ov2_o3_classic`, `~/gem5_arm/ov2_o3_oct_v2`):

| metric, core 0 (node thread) | classic | Octopus |
|---|---:|---:|
| IPC | 1.50 | 0.94 |
| load-to-use, mean cycles | 5.35 | 10.81 |
| L1D misses | 23.4 M | n/a |
| invalidations delivered to the core by the L1 | n/a | 11.0 M |
| rename SQ-full events | 65.8 M | 434 M |
| memory-order violations | 1.14 M | 2.03 M |

The 11.0 M invalidations are lines the L1 held in a readable state and lost. Only
remote writes change data; the rest are LLC back-invalidations and the L1's own
replacements. Classic delivers none of the former, because its L2 never recalls a
line. The extra memory-order violations are the LSQ squashes those invalidations
cause, about one to two percent of the cycles; the expensive part is that the lines
are gone and the loads that follow miss.

The ratio makes it structural: the L1s hold 4 MiB, half of the LLC's 8 MiB, and
the LLC's LRU is updated only by L1 misses, so the lines a core uses most look
coldest to the LLC. Real inclusive LLCs keep the core caches at a small fraction of
the LLC and steer replacement away from lines the cores still hold (Jaleel et al.,
"Achieving Non-Inclusive Cache Performance with Inclusive Caches", MICRO 2010).

## Current behaviour (code)

- `Protocols_FSM/MSI_LLC.csv`: `IorS + Replacement = IssueInv/` and
  `M + Replacement = IssueInv/`; the LLC then hears its own invalidation back and
  runs `WriteBack/N`. The invalidation is a bus broadcast; there is no sharer
  vector in the snooping LLC (only `SetOwner`), so every L1 processes it.
- `Protocols_FSM/MSI_splitBus_snooping.csv`: `S + Invalidation = I`,
  `M + Invalidation = Data2Req/I`. `CacheController::updateCacheLine` reports the
  loss to the gem5 bridge (`isReadableState` old/new), which delivers an
  invalidation snoop to the O3 core.
- A clean L1 eviction (`S + Replacement = I`) is silent: the LLC never learns that
  an L1 dropped a line.
- The LLC has no dirty bit; a clean victim is written back to memory in full
  (`WriteBack`), and the victim's read-out shares the fill's array slot.
- Replacement: `CacheDataHandler_COTS` LRU over the LLC's own accesses; L1 hits are
  invisible to it.
- Directory presets (`LLCMSIDirectory` and derived) keep a sharer list per line
  (`protocol_counters`), but `MSI_LLC_directory.csv` also sends `SendInv` on
  `Replacement`, so the directory variant is inclusive too.

## What the references do

- **gem5 classic**: L2 `clusivity = mostly_incl`. An L2 eviction does not recall the
  line; the L2 crossbar's `SnoopFilter` tracks which L1 ports hold each line
  (`src/mem/snoop_filter.cc`, `lookupRequest`), an L2 eviction packet carries
  "still cached above" (`Cache::evictBlock`, `setBlockCached`) so the upstream
  filter keeps the cluster as a holder, and the L2 forwards any snoop upward
  whether or not it holds the block. Clean L1 evictions are announced (`CleanEvict`).
- **gem5 Ruby CHI**: back-invalidation on deallocation is a per-node parameter
  (`dealloc_backinv_unique/shared`, `src/mem/ruby/protocol/chi/CHI-cache.sm`). The
  stdlib home node sets both to false and holds no data at all; it is a directory
  with a snoop filter (`src/python/gem5/components/cachehierarchies/chi/nodes/directory.py`).
  Only the private L2 back-invalidates its own L1.
- **gem5 Ruby MESI_Two_Level**: inclusive L2, back-invalidates sharers on
  replacement, no protection; lives with the storm because its L1s are small.

## Proposal

Two deliverables, independently useful. B is the feature; A is the cheap step that
already removes most of the storm and is needed by B anyway.

### A. Presence-aware inclusive LLC

1. **Presence bits** per LLC line (one per upper controller): set when the LLC
   supplies a line to an L1 (`SendData`, `SendExeclusiveData`, and when an L1 fill
   is observed on the bus), cleared by `PutM` and by a new **clean-evict notice**
   from the L1 (`S + Replacement` sends it; today silent). Stale-set bits are
   conservative (a line is protected a little longer), never unsafe.
2. **Replacement** prefers victims with no presence bits (query-based selection):
   `CacheDataHandler_COTS::findVictim` gets a "prefer untracked" pass before LRU.
3. **Targeted back-invalidation**: `IssueInv` carries the presence set; an L1 not in
   it ignores the message (or the bus delivers it only to those ports). Traffic
   drops; correctness unchanged.
4. Knob `llc_controller.presence_aware_replacement(i)` (default 0 so the lab presets
   stay byte-identical).

### B. Non-inclusive LLC with a tracking directory

1. **Directory structure** beside the data array: entries of {tag, sharer vector,
   owner, dirty hint}, no data, with its own sets/ways
   (`llc_controller.snoop_filter_sets/ways`). Sized to cover at least the sum of the
   L1 capacities with headroom, as real snoop filters are.
2. **Data eviction never invalidates**: `Replacement` of LLC data on a line that is
   tracked keeps the directory entry and drops (or writes back, if dirty) the data
   only. New LLC state `T` (tracked, no data) in the tables, with rows:
   - `T + GetS`: if an owner exists, the owner supplies (`Data2Both` already does
     this on the snooping bus; the LLC saves the data or not, configurable); else
     fetch from memory.
   - `T + GetM`: invalidate sharers by the vector, owner supplies or memory does.
   - `T + PutM`: allocate data (write-allocate) or write around.
   - `T + Own_Invalidation`: never issued for data eviction.
3. **Directory eviction does invalidate**: when a directory entry must be replaced
   (capacity), back-invalidate the holders in its vector, exactly like a hardware
   snoop filter. This is the only remaining back-invalidation and it is bounded by
   the directory's size, not the data array's.
4. **Dirty bit** in the LLC line so clean victims are dropped without a memory
   write, and the victim read-out charged its own array slot.
5. **Config**: `llc_controller.inclusion(s) = inclusive | non_inclusive` (default
   inclusive: lab presets unchanged). MESI and MOESI LLC tables get the same rows;
   the directory presets align their `Replacement` rows with the same rule.
6. **Bridge**: no change. Losses reported to the core simply become rare.

## Validation

- Lab presets (`MultiCoreSystem_Directory`, `MultiCoreSystem_Snoop`) byte-identical
  with the defaults; the standard regression.
- Harnesses: extend `gem5/harness/evict.cc` with an L1-held victim (non-inclusive:
  the L1 must keep the line and later hit; presence-aware: the victim must be a
  line no L1 holds while one exists); `stress.cc` across both modes and seeds.
- Add a per-cause counter at the L1 (remote write / LLC back-invalidation / own
  replacement) so the 11 M can be attributed before and after.
- ov2slam A/B from the `ov2_start` checkpoint: invalidations to core 0, L1D miss
  equivalent, load-to-use, IPC, memory-order violations, against classic and
  against the current Octopus run. Expect the L1 loss count to approach classic's
  23 M misses and load-to-use to fall toward classic's.
- llsc: unchanged PASS with the same tick counts in inclusive mode.

## Cheap experiments to run first

1. Rerun ov2slam with 64 KiB L1s in both arms. If most of the 11 M losses vanish,
   the preset's ratio is the cause and the feature is about fidelity at large L1s.
2. Add the per-cause counter (one afternoon) to size A against B.

## Tasks

- [ ] Per-cause invalidation counter at the L1 and in the bridge exit summary
- [ ] Clean-evict notice from the L1 (`S + Replacement`), ignored by the inclusive LLC
- [ ] A1-A4 presence bits, replacement pass, targeted invalidation, knob
- [ ] B1 directory structure and parameters in the COTS handler
- [ ] B2-B3 `T` state rows in `MSI_LLC.csv`, `MESI_LLC.csv`, `MOESI_LLC.csv`; directory-entry eviction path
- [ ] B4 dirty bit and victim slot accounting
- [ ] B5 `inclusion` knob, directory presets aligned
- [ ] Harness extensions, regression, ov2slam A/B, llsc

## References

- `gem5/COMMIT_PLAN.md`, commit 11 background (one access per line) and the
  improvements section.
- gem5 classic: `src/mem/cache/base.cc` (`clusivity`), `src/mem/snoop_filter.cc`.
- gem5 Ruby CHI: `src/mem/ruby/protocol/chi/CHI-cache.sm` (`dealloc_backinv_*`).
- A. Jaleel, E. Borch, M. Bhandaru, S. C. Steely, J. Emer, "Achieving Non-Inclusive
  Cache Performance with Inclusive Caches", MICRO 2010.
