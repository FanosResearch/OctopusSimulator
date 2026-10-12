# Logger redesign — Design B (self-describing events)

> **STATUS: IMPLEMENTED & VALIDATED (all phases).** The event timeline is now the sole
> decomposition; the legacy checkpoint path is removed. `docs/Logger.md` documents the shipped
> design. Validation summary:
> - **Tiling `Σstages == Total`: 275304/275304** per run (EEMBC a2time01, both snoop & perfect-LLC).
> - **EEMBC suite: 10/10** complete, tiling `fail=0`, no faults (snoop). **Directory: 3/3**.
> - **Phase-3 A/B (event vs legacy):** Total identical (0 mismatches), upstream stages match every
>   data row, Response-Bus cleaned (worst 536→104, avg 21.4→1.8 — up to 331 cyc of L2-stall removed).
> - **Phase-4 (legacy removed):** event output **byte-identical** to Phase-3 under the same config;
>   run-to-run **deterministic**.
> - The registry (§3) was **subsumed by role-carrying events** (each event names its `Role`), which
>   already delivers the self-describing goal without a global-id refactor; the comp_id-keyed
>   registry was dropped (the id scheme aliases CPU↔L1 and bus↔core).

*Status of the plan below: as-built (kept for the rationale/phasing record).* Replaces the overloaded, position-inferred checkpoint model of the current
`Logger` (`src/Logger.cpp`, documented in `Logger.md`) with **self-describing timeline events**, so
per-stage latency is computed by a **generic walk**, never by guessing a checkpoint's role from its
index or the vector's size.

## 0. Why (the defect being removed)

Today `log_entries[msg_id]` is `vector<uint64_t>[NUM_OF_ELEMENTS_PER_ENTRY]` — one vector per
`EntryId`. Two of those vectors are **multiplexed across several resources/phases**:
- `CACHE_CHECKPOINT` = { L1-admit, LLC-admit, LLC-data-access, **L1-post-response-fill** }.
- `RESP_BUS_CHECKPOINT` = { bus0 LLC→L1, bus1 LLC→DRAM, bus1 DRAM→LLC }.

`calculateLatencies` recovers roles by **position + count** (`size()==3`, index `[2]`), which is
empirically wrong: LLC hits are `cache=2` *or* `cache=3`, misses are `cache=2` (not 3), and the 3rd
cache stamp is a post-response L1 fill in 43% of `cache=3` hits. Result: the hit `Response-Bus`
column swallows up to 331 cycles of L2-Stall, and the miss `L2-Access` branch is dead. (See the
`Logger.md` audit + empirical validation.)

## 1. Goals / non-goals

**Goals**
- Every checkpoint is **self-describing**: it records *which component* and *which phase*, so the
  decomposition needs no positional inference.
- Per-stage latencies **tile exactly**: `Σ residencies == Total` for every request, by construction.
- **Topology-agnostic**: adding an L3, a mesh hop, or extra DRAM stages needs *zero* changes to the
  decomposition — only new components register themselves.
- The Logger stays **passive/observational**: it must not change any timing (Total must be bit-identical
  to today).
- Preserve the existing external contract where cheap: `OCTOPUS_NO_LOG`, the per-core report +
  `Summary.csv`, worst-case-per-stage, memory-lean freeing.

**Non-goals**
- No change to coherence, arbitration, or the simulated hierarchy.
- Not adding new *measured* physics — only relabeling *where/what* we stamp.
- Not (yet) a general trace-export format; we keep CSV reports.

## 2. The event model

```cpp
enum class Phase : uint8_t { ENTER, SERVICE, EXIT };   // custody / service-start / handoff-out
struct LogEvent {
    uint32_t component_id;   // stable id from the component registry (§3)
    Phase    phase;
    uint64_t cycle;          // in the ORIGINATING CORE's clock (unchanged clock trick, §6)
};
struct LogEntry {
    uint64_t cpu_id, req_id, address, trace_cycle;   // identity (set once)
    std::vector<LogEvent> events;                    // time-ordered timeline
};
std::unordered_map<uint64_t /*msg_id*/, LogEntry*> log_entries;
```

**Semantics.** A component stamps `ENTER` when it takes custody of the message (admitted to its
queue / RX buffer), optionally `SERVICE` when it begins the actual work (e.g. LLC data-array access
starts), and `EXIT` when it hands the message to the next component (pushed to the next TX
buffer / delivered). Handoffs are same-cycle (`EXIT_X.cycle == ENTER_Y.cycle`), so intervals tile.

**Residency & sub-stages** (generic):
- Time *at* component X (queue + service) = `EXIT_X − ENTER_X`.
- Queue/stall at X = `SERVICE_X − ENTER_X`; service at X = `EXIT_X − SERVICE_X` (only where a
  `SERVICE` event is stamped; otherwise the whole residency is one stage).
- A component visited more than once (LLC on a miss: admit → send-to-DRAM, then refill → respond)
  produces two `ENTER…EXIT` spans; the walk sums or reports them per visit index (§5).

Post-completion maintenance (the old troublemaker — the L1 fill after `CPU_RX`) is stamped as its
own `L1 / SERVICE` event **after** the terminal CPU `ENTER`, and the walk stops at completion, so it
can never contaminate a forward stage.

## 3. Component registry (identity)

The Logger holds a registry populated at construction, before the run:

```cpp
enum class Role : uint8_t { CPU, L1, REQ_BUS, RESP_BUS, LLC, MEM_BUS, DRAM /*, L3, NOC … */ };
struct Component { uint32_t id; Role role; int level; std::string name; };
void Logger::registerComponent(uint32_t id, Role role, int level, const std::string& name);
```

- Each `CPU`, cache controller, bus, and memory controller is handed a `component_id` at
  construction by `MultiCoreSystem` (which already builds them all) and calls `registerComponent`.
- A **bus** is one component; the **channel** (request vs response) is encoded by the `Phase`/an
  extra channel bit, or by two registered ids (`bus0.req`, `bus0.resp`). Recommended: two ids per
  split bus, so `REQ_BUS` and `RESP_BUS` are first-class components (matches the split-transaction
  model and keeps the walk trivial).
- The registry is what maps a `component_id` → a **report column**, so the columns are derived from
  the *actual* topology, not hard-coded.

This removes the "L1 vs LLC share `CACHE_CHECKPOINT`" ambiguity: each controller stamps its **own**
`component_id`.

## 4. Stamp API & the exact sites to change

New API (replaces `updateRequest(msg_id, EntryId)`):
```cpp
void Logger::begin(uint64_t cpu_id, const Message& m);          // creates entry + CPU/ENTER
void Logger::event(uint64_t msg_id, uint32_t comp, Phase ph);   // append one event
// completion = the CPU stamps (CPU, ENTER) on receive -> triggers finalize()
```

Concrete sites (file:line are current; they move as code edits):

| Component / phase | Where to stamp | Replaces |
|---|---|---|
| CPU / ENTER (issue) | `CPU::addRequest` | `CPU_CHECKPOINT` |
| L1 / ENTER (admit) | `BaseController::processLogic` (the `source==LOWER` stamp) **when this ctrl is an L1** | half of `CACHE_CHECKPOINT` |
| L1 / SERVICE (data access) | `CacheController::processDataArrayBuffer` **when L1** | part of `CACHE_CHECKPOINT` |
| L1 / EXIT (emit to req bus) | `BaseController::sendBusRequest` (L1 miss → request bus) | — (new) |
| REQ_BUS / EXIT (transmit) | `BusController::broadcast` (bus0) | `REQ_BUS_CHECKPOINT` |
| LLC / ENTER (admit) | `BaseController::processLogic` (`source==LOWER`) **when LLC** | half of `CACHE_CHECKPOINT` |
| LLC / SERVICE (array access start) | `CacheController::processDataArrayBuffer` **when LLC** | part of `CACHE_CHECKPOINT` |
| **LLC / EXIT (emit response)** | `BaseController::hitAction` & `removePendingAndRespond` (at `pushMessage` down) | — (**the key new stamp**) |
| MEM_BUS / EXIT (LLC→DRAM) | `BusController::broadcast`/`send` (bus1) | part of `RESP_BUS_CHECKPOINT` |
| DRAM / ENTER, EXIT | `MainMemoryController` (arrive; data ready at `m_memory_latency`) | inferred today |
| MEM_BUS / EXIT (DRAM→LLC) | `BusController::send` (bus1) | part of `RESP_BUS_CHECKPOINT` |
| RESP_BUS / EXIT (transmit) | `BusController::send` (bus0) | `RESP_BUS_CHECKPOINT` |
| CPU / ENTER (receive) | `CPU::checkReceiveBuffer` → **finalize** | `CPU_RX_CHECKPOINT` |
| (Mesh/NoC variants) | `MeshController` 78/80, `NoCController` 53/55 | as above |

The two "**when L1 / when LLC**" rows are disambiguated by the controller's own registered
`component_id` — no new branching logic, the id is passed straight through.

## 5. Generic decomposition (`finalize`)

```
sort events by cycle (stable; they are already appended in time order)
Total = last.cycle − first.cycle
for each consecutive pair (e_i, e_{i+1}):
    charge (e_{i+1}.cycle − e_i.cycle) to the stage keyed by (e_i.component.role, e_i.phase)
```
- Each interval is attributed to the component that *held* the message during it, split into
  stall (`ENTER→SERVICE`) vs service (`SERVICE→EXIT`) where a `SERVICE` event exists.
- Multi-visit (LLC twice on a miss) → the key includes a visit index, or the two spans map to
  distinct legacy columns (visit 1 → "L2-Stall + L2→DRAM handoff", visit 2 → "refill + Response").
- `Σ intervals == Total` **exactly**, by construction — this is the invariant the current design
  violates.

### Legacy-column mapping (migration)
To avoid breaking every sweep parser / figure, `finalize` emits the **same CSV header** as today by
mapping role→column:
```
L1 residency            -> CPU?/L1-Stall (+ L1-access if SERVICE present)
REQ_BUS residency       -> Request-Bus
LLC visit1 stall        -> L2-Stall
LLC service (hit)       -> L2-Access
MEM_BUS out + DRAM + in -> L2-DRAM-Bus + DRAM
LLC visit2 (refill)     -> folded into L2-Access (miss) per current semantics
RESP_BUS residency      -> Response-Bus   (= RESP_BUS_EXIT − LLC_EXIT, now clean)
CPU receive tail        -> (absorbed into Total; optional new column)
Total, Effective        -> unchanged
```
So downstream tooling sees the *same columns, now correct*. A new opt-in verbose mode can dump the
full per-component timeline for debugging.

## 6. Invariants preserved

- **Clock trick:** `event()` still records `core_clk_count[originating core]`, looked up from the
  entry's `cpu_id`. Unchanged; all stamps remain in the issuing core's clock.
- **Passivity:** the Logger only reads clocks and appends events — **Total must be bit-identical**
  to the pre-change binary (a hard regression check, §8).
- **Memory-lean:** `LogEntry*` freed in `finalize`; only in-flight messages hold memory (one
  `vector<LogEvent>` instead of 9 vectors — same or less allocation).
- **`OCTOPUS_NO_LOG`:** `begin`/`event` early-return; `finalize` still registers cores + emits
  `Summary.csv` completion.
- **Worst-case tracking / `Summary.csv`:** `logMax` per stage as today, driven by the mapped
  columns; per-component worst-case is a natural superset.

## 7. Phasing (safe, incremental)

1. **Scaffolding (no behavior change):** add `Role`, `Component`, registry, `LogEvent`, and
   `begin/event/finalize` **alongside** the existing `updateRequest` path. Register components in
   `MultiCoreSystem`. Dual-write: every existing `updateRequest` also emits the equivalent
   `event()`. Reports still come from the old path. → build + full EEMBC/SPLASH must be unchanged.
2. **New stamps:** add `LLC/EXIT` (emit), `L1/EXIT`, cache `SERVICE`, explicit `DRAM` and split
   `MEM_BUS` events.
3. **Switch decomposition:** implement `finalize`'s generic walk + legacy-column mapping; write
   reports from the new path. Keep the old path behind a flag for A/B diffing.
4. **Validate (§8).** Then delete the old `EntryId` checkpoint path and the dead branches.
5. **Docs:** rewrite `Logger.md` to the event model; keep a short "legacy checkpoint model" appendix.

## 8. Validation plan

- **Tiling invariant:** debug assert `Σ stage intervals == Total` for **every** request across
  a2time01 + at least one SPLASH bench (snoop MESI + directory). Must be 100%.
- **Total regression:** `Total`, `Effective`, and `Finish Cycle` must be **identical** to the
  pre-change binary on the full EEMBC set (proves passivity — timing untouched).
- **Upstream cross-check:** `L1-Stall`, `Request-Bus`, `L2-Stall` (correct today) must match the old
  values row-for-row.
- **Response-Bus correctness:** new `Response-Bus == RESP_BUS_EXIT − LLC_EXIT`; spot-check the known
  contaminated rows (e.g. the 256-cycle-L2-stall hit) now report the true ~18-cycle bus leg.
- **No faults / completion:** EEMBC 9/9 + SPLASH suite complete, footers present.

## 9. Risks & mitigations

| Risk | Mitigation |
|---|---|
| A stamp site missed → gap in the walk | The tiling assert (§8) catches any gap immediately; dual-write phase (§7.1) lets us diff. |
| Same-cycle event ordering ambiguity | Stable insertion order + a documented tiebreak by `Phase` (ENTER<SERVICE<EXIT). |
| Report-format churn breaks figures | Legacy-column mapping (§5) keeps the CSV header identical. |
| Multi-visit LLC mis-mapped | Explicit visit index in the walk key; unit-test the miss path timeline. |
| Perf regression (many small events) | One `vector<LogEvent>` per msg (reserve small); fewer allocs than 9 vectors. |
| Scope creep vs the DATE deadline | Design A remains the fast path; Design B can land after the paper if needed (§10). |

## 10. Effort & relation to the deadline

- Rough effort: **scaffolding ~0.5 day, stamps + walk ~1 day, validation + doc ~0.5 day** (~2 days).
- For the **DATE deadline**, Design A (named per-resource checkpoints, incl. the single `LLC_EXIT`
  stamp) gives clean angle-A/C numbers in a few hours and is a strict *subset* of Design B's stamps
  — so doing A now does **not** throw away work; B later just generalizes A's fixed columns into the
  registry-driven walk. Recommended: **A before the deadline, B after** (or B now only if we have
  slack).

## 11. Doc deliverable

Rewrite `Logger.md`: event model (§2), component registry (§3), the generic walk (§5), the tiling
invariant, and a short appendix mapping the old checkpoint columns to the new events (for anyone
reading pre-change results). Remove the idealized `[L1, L2, fill]` claims.
