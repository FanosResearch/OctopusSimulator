# ESSC demo — end-to-end plan (Octopus-backed real-time tracking)

Goal: turn `octopus-tracking-demo.html` from a queueing-model mock-up into a demo whose pen is
driven by **real Octopus traces**, for the ESSC competition. This plan is the path from an
Octopus run to the on-screen pen. Companion to `octopus-rt-demo-notes.md` (the demo internals).

---

## 0. The one-line story the demo tells

A mobile robot drives from **start to goal**, following a **ground-truth reference path**. Its
localization/planning control task runs periodically on core 0 and shares the memory system with
three other cores. **When those cores contend, the localization job finishes late, the robot acts
on a stale pose, and its actual path deviates from the ground truth — cutting corners, drifting,
missing the goal. A predictable, work-conserving arbiter (Round-Robin) in Octopus bounds the
memory latency, the job finishes on time, and the robot re-converges onto the reference path.**
Every millisecond on screen comes from a cycle-accurate Octopus run.

**Two changes from the first mock-up:**
- **Arbiter = Round-Robin, not TDM.** TDM bounds latency but is ~14× slower (impractical run
  times + a weaker on-screen story). RR is **still predictable** — it bounds core 0's worst-case
  bus wait to one rotation, unlike FCFS which can starve it under a contender burst — and it is
  work-conserving, so runs finish fast. The real-time argument (bounded WCL) holds; the contrast
  is FCFS-unbounded (Act 2) vs RR-bounded (Act 3).
- **Metaphor = SLAM/robot navigation, not a pen plotter** (see §1b).

## 1b. Visualization metaphor — robot navigation (SLAM-style)

Same underlying data model (job finish-time → tracking error); only the visuals change.

- **Scene:** top-down 2D map. `START` and `GOAL` markers, optional obstacles. A faint
  **ground-truth path** (the planned ideal trajectory start→goal).
- **Robot:** an icon that moves toward the goal, steering to follow the ground-truth path using
  the **most recent pose estimate** produced by the control task.
- **The control task = localization/planning.** Each period it produces a fresh pose/plan. If the
  job finishes on time → fresh pose → the robot hugs the ground-truth line. If it finishes late
  (contention) → the robot steers on a **stale pose by exactly `C`** → position error grows: it
  drifts, cuts across curves, overshoots turns, and can miss the goal (or clip an obstacle).
- **On-screen:** ground-truth path (faint), actual trajectory (bold, deviating), robot icon,
  start/goal, a shaded **error corridor** between actual and ideal, and readouts for
  *localization staleness (ms)*, *position error*, and *deadline misses*.
- **The three acts** read as: robot tracks perfectly (Alone) → robot wanders off / misses goal
  (Contention) → robot re-converges and reaches the goal (Protected-RR).

The mapping to the existing engine is 1:1: the old "pen chases the last setpoint at a speed
limit" rule becomes "robot steers toward the goal using the last pose estimate"; lag under long
`C`, straight chords across curves, and corner overshoot all still fall out of the same finish-time
model. So the redesign is a **rendering + labeling** change, not a new simulation.

---

## 1. The three acts map to three Octopus configs

| Act | Octopus config | What it shows |
|-----|----------------|---------------|
| **1. Alone** | core 0 control task, cores 1–3 idle, **FCFS** bus | baseline: control job ~short, on the line |
| **2. Contention** | core 0 control task + cores 1–3 streaming, **FCFS** bus | latency inflates (cache pollution + bus/DRAM queueing), jobs overrun, pen lags |
| **3. Protected** | same 4-core load, **RR** bus (+ optional LLC way reservation for core 0) | bounded core-0 wait; job fits the period; robot re-converges |

Act 3's "protection" **is RR predictable arbitration** (bounded worst-case bus access, work-
conserving). We validated FCFS/RR/TDM all complete this session; RR is the fast, predictable
choice for the demo. Relabel the page accordingly (see §6). (Keep TDM available as a "maximally
predictable but slow" aside if asked, but do not drive the demo with it.)

---

## 2. Workload design (trace-driven — we synthesize the traces)

Octopus is trace-driven; each core reads `trace_C{n}.trc.shared` (`addr  <flags>  R/W  seq`).
We generate four synthetic traces that make the three acts legible:

- **Core 0 — the control task.** A periodic burst of `A` accesses (default `A = 64`, matching
  the demo's `P.accesses`) over a **small working set** (a few cache lines = the control state +
  gains), one burst per control period, repeated for ~N periods. Small footprint → hits when
  alone, gets **polluted** out of the shared LLC under contention.
- **Cores 1–3 — the contenders.** Long **streaming** access patterns over a **large working set**
  (stride ≥ line size, working set ≫ LLC) → high LLC miss rate, saturate the response bus + DRAM.
  Tunable "intensity" = accesses per unit time.

One generator script (`gen_demo_traces.py`) with knobs: `A`, `periods`, control-working-set size,
contender-working-set size, contender intensity. Emits the 4 `.trc.shared` files into a new
`BMs/demo-rt/` workload dir.

> Note: Octopus has no notion of period/deadline — it just replays accesses. The **control
> semantics (period, deadline)** are imposed by us in extraction (§4) and the demo. That is
> honest: Octopus supplies the *timing*, we overlay the *schedule*.

---

## 3. The Octopus runs (three, one per act)

Same workload dir, three configs (all snoop-MESI + CacheControllerExclusive, the validated split
FSMs once integrated):

```
# Act 1 — alone (cores 1-3 traces empty/absent), FCFS
Octopus -s MultiCoreSystem -p workload_path=BMs/demo-rt-alone/   (bus arbiter FCFS)
# Act 2 — contention, FCFS
Octopus -s MultiCoreSystem -p workload_path=BMs/demo-rt/          (bus arbiter FCFS)
# Act 3 — contention, RR  (+ optional LLC way reservation for core 0 if supported)
Octopus -s MultiCoreSystem -p workload_path=BMs/demo-rt/          (bus arbiter RRArbiter)
```

Each writes `newLogger/LatencyReport_C0.csv` — the core-0 control task's per-access latencies.
(Cores 1–3 reports are not needed by the demo.)

---

## 4. Job aggregation — per-access report → per-job JSONL

`LatencyReport_C0.csv` columns (confirmed; `L1 Access` added 2026-09-20, so Total is
col 13 and Effective col 14):
`RequstID, Request Address, Trace Cycle, CPU Latency, L1 Stall, Requst Bus, L2 Stall,
L2 Access, Response Bus, L2-DRAM Bus, DRAM, L1 Access, Total Latency, Effective Latency`.

Group core-0 accesses into control jobs of `A` consecutive accesses. Per job emit the schema the
demo already expects:

```json
{"job": k,
 "release_cycle": <Trace Cycle of access[0]>,
 "finish_cycle":  <Trace Cycle of access[A-1] + Total Latency of access[A-1]>,
 "n_accesses": A,
 "mean_lat_ns": mean(Total Latency over the A accesses)  * CYCLE_NS,
 "max_lat_ns":  max(Total Latency)                        * CYCLE_NS,
 "llc_misses":  count(DRAM latency > 0),
 "deadline_miss": (finish_cycle - release_cycle) > PERIOD_CYCLES }
```

- `CYCLE_NS` — sim-cycle→ns scale (calibration, §7).
- `PERIOD_CYCLES` — the control period we impose (choose so Act 1 fits, Act 2 overruns).
- One JSONL file per act: `demo/data/act1_alone.jsonl`, `act2_contention.jsonl`, `act3_tdm.jsonl`.

Extraction is a ~30-line `extract_jobs.py` (or awk). It also emits a tiny `manifest.json` with
per-act summary (mean/max job time, deadline-miss count) for the captions.

---

## 5. Replay wiring in the HTML (lowest-coupling, conference-safe)

Per the notes, **replay** is the safest wiring (zero coupling, never stalls). Changes to the page:

1. **Data store.** Add `S.replay = { act1:[...], act2:[...], act3:[...] }`, loaded from the JSONL
   (fetch on load, or a **file-drop handler** so the hosted copy works too).
2. **Swap `sampleJob()`.** When replay data is present, `releaseJob()` pulls the next record for
   the current act instead of calling the model: `L = mean_lat_ns`, and job execution time
   `C = (finish_cycle - release_cycle) * CYCLE_MS`. `deadline_miss` and `llc_misses` drive the
   readouts directly. Keep the model as a fallback when no data is loaded (offline demo still works).
3. **Bound line.** `S.bound` (act 3's worst-case guide) = max job time in `act3_tdm.jsonl` — a
   *measured* bound, not a formula.
4. Everything downstream (robot position/heading, path deviation, error corridor, deadline
   counter) derives from `finish_cycle`/`C` — no other changes to the engine.

Result: same engine, now every millisecond is Octopus — rendered as a robot on a map (§1b).

---

## 6. Relabel the "protection" to match Octopus (honesty pass)

The mock says "way reservation + bandwidth budget + predictable arbitration." Octopus's real,
validated mechanism for the demo is **RR predictable arbitration** on the request/response bus. So:

- Make **RR arbitration** the headline of Act 3 (`CAPTIONS.octopus`, `.gate`/`.part` SVG, `mcNote`).
  The point: RR bounds core 0's worst-case bus wait to one rotation, so the localization job's WCL
  is analysable; FCFS gives no such bound under a contender burst.
- **Way reservation** — include only if we actually enable LLC cache partitioning for core 0
  (check `configuration` for per-core way allocation; if unsupported, drop that label).
- **Bandwidth budget** — drop unless we add a real regulator; RR already bounds bus access.
- Add a one-line honest footnote: "Latencies replayed from Octopus cycle-accurate runs;
  arbitration = Round-Robin." A competition strength, not a caveat.

---

## 7. Calibration (cycle → ns, and the period)

- `CYCLE_NS`: Octopus reports latency in sim cycles; pick the scale from the config's clock period
  (bus `m_clk_period` = 50 ns is a starting point) or simply choose a scale that puts Act-1 hits
  near ~14 ns so absolute numbers read plausibly. Relative inflation (the real result) is
  scale-invariant.
- `PERIOD_CYCLES`: set so Act 1's jobs finish well inside it and Act 2's overrun — read the Act-1
  and Act-2 job-time distributions and place the period between them. This is presentation tuning,
  documented in `manifest.json`.

---

## 8. Execution model & venue — offline run, then replay (confirmed)

**The demo does NOT run Octopus live.** Octopus runs **to completion, offline, once per act**; we
capture the `LatencyReport`, extract the per-job JSONL (§4), and ship those three files with the
page. The browser then **replays** them at a chosen playback rate — so it *looks* real-time and is
smooth, deterministic, and never stalls (a cycle-accurate sim is orders of magnitude slower than
wall-clock, so live playback of a control loop is not feasible on a booth).

**Recommend: canned replay from the three JSONL files** for the ESSC floor. Works from the hosted
page with a file-drop too. The streaming bridge (Python tail → WebSocket, "watch it simulate")
stays an optional lab/class variant, not the competition booth.

---

## 9. Deliverables & sequence

1. `gen_demo_traces.py` → `BMs/demo-rt*/` traces  *(workload)*
2. Three Octopus runs → three `LatencyReport_C0.csv`  *(data)*
3. `extract_jobs.py` → `demo/data/act{1,2,3}.jsonl` + `manifest.json`  *(aggregation)*
4. HTML: file-drop + replay swap + measured bound  *(wiring)*
5. Relabel Act 3 to TDM; honesty footnote  *(honesty)*
6. Calibrate `CYCLE_NS` / `PERIOD_CYCLES`; final visual polish  *(tuning)*

Each step is independently testable; the page keeps working (model fallback) until step 4 lands.

---

## 10. Risks / open items

- **Integration dependency.** The split MESI FSMs (this session) aren't the config default yet;
  the demo runs should use them (or FCFS/RR, which are unaffected). Alone/contention can even run
  on the committed FSMs since they don't deadlock; only if we want TDM on a deadlock-prone pattern
  do we need the split FSMs. For the *control+stream* workload, confirm which FSM set to pin.
- **Way reservation support** — verify before claiming it (see §6).
- **Cycle-unit meaning** — confirm `Total Latency` units before fixing `CYCLE_NS`.
- **Trace realism** — the synthetic control task should look like a plausible controller footprint;
  optionally swap in a real control-loop trace later.
- **Export button** (nice-to-have) — dump the replayed run as CSV for a paper figure (reuses the
  same JSONL).

---

## 11. Status — 2026-09-23 (implemented)

| step (§9) | state |
|---|---|
| 1. workload | **done differently**: no synthetic traces; the periodic-task layer is in the simulator (`docs/Tasks.md`, `task_C<n>.task.csv`, `JobReport_C<n>.csv`). Workloads: `demo/workloads/alone` (core 0 only, cores 1–3 idle) and `demo/workloads/contention` (core 0 + three streamers on disjoint regions, `ooo 8`, `start 6000`). Sources in `demo/tasks/`. |
| 2. runs | `demo/run_acts.sh` runs the three acts on the sweep's pinned config (MESI + Exclusive L1, MCsim DDR4 FR‑FCFS): act1 FCFS alone, act2 FCFS contention, act3 **RR bus + LLC `way_partition=0:0`** (way reservation is now a real Octopus mechanism: `CacheDataHandler`, masked LRU/Random). |
| 3. aggregation | `demo/extract_jobs.py` → `demo/data/act{1,2,3}.jsonl` + `.manifest.json` (per job: release/finish/deadline/miss/skipped, mean/max access latency, LLC misses, L1 hits). |
| 4. wiring | page replays the records (embedded by `demo/build_page.py`, or fetched from `demo/data/`, or dropped); the model fallback is gone. Time unit = simulated cycles. |
| 5. honesty | Act 3 relabelled to what Octopus models: Round‑Robin bus + one reserved LLC way; captions and the schematic carry the measured numbers from the manifests; footer states the source. The bandwidth‑budget claim is dropped. |
| 6. tuning | period **T = 5 000 cycles** (alone jobs 4 190; contended jobs overrun); robot metaphor (§1b) rendered: planned path, heading triangle, error corridor, start/goal marker, pose age readout. |

Measured (200 periods each): act1 0 misses, exec 4 190; act2 57 jobs released, 143 periods skipped,
exec p90 17 k / max 181 k cycles — core 0's DRAM reads wait 4.5 k cycles on average and up to 31 k
behind the streams' row hits (FR‑FCFS starvation, the paper's O3 live); act3 identical to act1.
Contention is a cliff: with the streamers at `ooo 4` the task is barely touched (3 misses), at
`ooo 6–8` it collapses; there is no gentle middle with these knobs, so the demo shows the collapse.
Without `attr,start` the protected act's cold first job still waited ~40 k cycles at DRAM
(arbitration cannot bound the memory controller) — the streamers now start after job 0.

## 12. Live mode — watching a run as it simulates (2026-09-24)

Octopus flushes every `JobReport_C0.csv` row as the job finishes, so the whole live path is a tail
of one file. Two new columns (`mean_access_lat`, `max_access_lat`, the core-observed access latency
of the job) make that file self-sufficient for the page.

    bash demo/live.sh act2            # configures the act, starts Octopus, serves the page
    # open http://localhost:8770/octopus-tracking-demo.html and press "Live"

`demo/live_server.py` tails the report and answers `GET /api/live?since=<job>` with the records
produced so far (same shape as the canned JSONL, so live and replay share one path) plus
`running`. The page **polls** every 250 ms — chosen over server-sent events because a missed poll
simply retries and there is no connection to re-establish on a booth. When the playhead reaches
the last reported job it **freezes** (the robot holds its last waypoint) and the badge reads
"waiting for the simulator"; when the run ends the act loops like the canned ones.

Pacing, honestly: these acts simulate far faster than the page plays them (act 2 is ~4 s of wall
clock for ~1 M cycles, which the page shows in ~84 s at 12k cycles/s), so live means *the data
arrives during the run and the view plays behind the write head*, not the page waiting on Octopus.
A heavier workload (more jobs, SPLASH-scale aggressors) inverts that.

## 13. Compare view - the three acts at once (2026-09-24)

The acts used to differ by an edit to the shared arbiter CSV, so only one could run at a time.
The bus arbiter turns out to be settable per run (`-p bus[0].interconnect_controller.arbiter_type(s)=...`,
verified by forcing TDM against an FCFS file and getting a different result), so each act now has
its own workload directory and differs only by `-p` overrides. Consequences:

- `run_acts.sh` runs the three **in parallel**: 4 s instead of ~20 s, same results to the byte.
- `live.sh compare` starts all three simulations at once and serves them together;
  `live_server.py` watches several runs (`--watch act1=dir --watch act2=dir ...`) and answers
  `GET /api/live?since=act1:12,act2:8,act3:0` with each act's new records.
- The page has a **Compare** button (canned or live): three robots, one map, one shared clock,
  each with its own trail, plus a side-by-side table (job time, missed periods, error, peak).
  The single-act view is unchanged. Because the clock is shared, the comparison is
  time-aligned: if one act's simulator is behind, the whole view holds rather than letting the
  others run ahead.

Not done yet: a "Run" button in the page (the server would spawn the simulator itself).

Open: a real profiled compute budget for the EKF (§10.1); browser check of the rebuilt page;
optional export button.
