# Spec — periodic-task layer for the Octopus CPU (for the ESSC RT demo)

Purpose: make a periodic real-time control task a **first-class citizen** in Octopus, so the
simulator itself reports per-job timing (release, finish, deadline hit/miss) with **real compute
budgets + cycle-accurate contended memory**. Thin extension to the existing `CPU` class; fully
backward compatible with today's traces.

Status: SPEC ONLY — no code yet. Review the "Open questions" (§10) before implementation.

---

## 1. Scope & non-goals

**In scope**
- A periodic task = a fixed **body** (compute phases + memory accesses) re-run every **period**.
- Octopus emits `JobReport_C{n}.csv`: one row per job with release/finish/deadline/miss.
- Reuses the existing compute model (`compute_time`) and MLP knob (`m_number_of_OoO_requests`).

**Non-goals** (explicitly out)
- No data-dependent compute (Octopus has mock data). Compute is a fixed cycle budget.
- No preemption / multi-task scheduling on one core. One periodic task per control core.
- No change to the memory system, protocols, or arbiters.

---

## 2. Task-program format — CSV (`<name>.task.csv`)

Octopus is CSV-throughout (FSMs, configs), so the task is a **CSV**. Uniform layout: **column 1 is
the row *kind***; the rest are kind-specific. Two kinds: `attr` (periodic attributes) and phase
rows (the body, top-to-bottom). Periodic mode is **opt-in** — a `*.task.csv` workload triggers it;
a legacy `trace_C{n}.trc.shared` uses the old path unchanged (§7).

Vocabulary is borrowed from the surveyed tools (§11): `COMPUTE` (Platform Architect processing),
`READ`/`WRITE`/`LINEAR`/`RANDOM` (gem5 generators), `period`/`deadline` attrs (SimSo).

```csv
kind,a,b,c,d,comment
attr,period,50000,,,control period in CPU cycles (= implicit deadline)
attr,jobs,200,,,number of jobs then finish
attr,ooo,1,,,outstanding-request window (1 = in-order)
attr,base,0x600000,,,working-set base (reference)
COMPUTE,300,,,,compute phase: advance 300 CPU cycles, no memory
READ,0x600040,,,,memory access: address in col a
WRITE,0x600000,,,,
LINEAR,0x600080,5,64,1.0,burst: base, count, stride, rd_ratio(0-1)
RANDOM,0x2000000,0x400000,1024,0.85,base, range, count, rd_ratio
```

**Column meaning by kind**

| kind | a | b | c | d |
|---|---|---|---|---|
| `attr` | name (`period`/`jobs`/`ooo`/`deadline`/`base`) | value | — | — |
| `COMPUTE` | cycles | — | — | — |
| `READ`/`WRITE` | address (hex) | — | — | — |
| `LINEAR` | base | count | stride | rd_ratio |
| `RANDOM` | base | range | count | rd_ratio |

- `attr` rows set the timing contract (self-contained with the task; still `-p`-overridable).
- Phase rows are the job body, replayed once per period; `compute_time` of an access = the
  `COMPUTE` cycles preceding it since the last access (or job start).
- `deadline` attr is optional; absent ⇒ implicit deadline = `period`.
- Contenders (cores 1–3) need only `LINEAR`/`RANDOM` rows + `jobs,-1` (loop) — no hand traces.

**Worked examples (in `demo/tasks/`):**
- `localization_C0.task.csv` — EKF pose-localization RT task: ~20 accesses over ~9 lines +
  ~4,150 compute cycles per job, over a small evictable working set.
- `streamer_C1.task.csv` — pure generator aggressor: `LINEAR` sweep (256 KB) + `RANDOM` thrash
  (4 MB), `ooo=4`, `jobs=-1`.

Authoring source: set the `COMPUTE` budgets and access pattern from a **profiled/WCET-measured**
real localization/planner (Platform-Architect TGG style, §11), so neither compute nor accesses
are invented.

---

## 3. Periodic execution semantics (exact)

Let period = `T`, job index `j = 0,1,2,…`. Non-preemptive, single instance (no job queue).

```
release_j  = max(finish_{j-1}, j * T)      # can't start before its period boundary or the prev job
deadline_j = j * T + T                      # implicit deadline = period
finish_j   = cycle when the LAST access of job j's body completes (+ trailing compute)
miss_j     = finish_j > deadline_j
```

- The CPU idles (issues nothing) from `finish_{j-1}` until `release_j` when a job finishes early.
- On overrun (`finish_j > deadline_j`): `miss_j = true`; the next job's release is pushed to
  `finish_j` (so a long job delays its successor — this is what makes the robot fall behind).
- If a job overruns by ≥ one period, intermediate releases are skipped (`release` jumps to
  `finish`), and each skipped deadline is counted as a miss (report a `missed_deadlines` count).
- MLP: with `OOO=1` accesses serialize (in-order blocking core — clean compute↔memory sequencing,
  recommended for the control task). `OOO>1` overlaps accesses; `finish` = last completion.

---

## 4. CPU changes (minimal, where)

New protected members on `CPU`: `m_period`, `m_jobs_total`, `m_job_index`, `m_job_release`,
`m_job_deadline`, `m_body_start_pos` (file offset), `m_job_accesses`, `m_periodic` (bool).

- **`init()`**: peek the file head; if `PERIOD` present → set `m_periodic=true`, parse directives,
  record `m_body_start_pos` at the first body line, set `m_job_index=0`, `m_job_release=0`.
- **`readSampleFromWorkload`**: in periodic mode, recognize `C <k>` → a *compute-only* sample
  (`compute_time=k`, `msg=NONE`); recognize body EOF → signal "job body done".
- **`processLogic`**: 
  - Gate job start on `m_clk_cycle >= m_job_release`.
  - Compute-only sample: advance the issue clock by `compute_time` (no `pushMessage`).
  - Access sample: existing issue+await path (OoO-gated).
  - On "job body done" **and** all its accesses have returned: set `finish`, compute `miss`,
    write a `JobReport` row, then `m_job_index++`; if `< m_jobs_total`, rewind the file to
    `m_body_start_pos`, set `m_job_release = max(finish, m_job_index*T)`; else finish the core.
- **`checkReceiveBuffer`**: unchanged, plus decrement an in-job outstanding counter so we can tell
  when the job's last access has returned.

Estimated size: ~50–70 lines, isolated behind `m_periodic`. Legacy path byte-identical.

---

## 5. `JobReport_C{n}.csv` schema (Octopus-emitted)

One row per job — the timing truth from the simulator:

```
job, release_cycle, finish_cycle, deadline_cycle, deadline_miss, n_accesses, missed_deadlines
```

Deliberately **not** including per-access latency stats here — those live in the existing
`LatencyReport_C{n}.csv` (all stages) and are joined in post (§6). Keeps the CPU change tiny and
avoids duplicating the Logger.

---

## 6. Join to the demo's per-job JSONL (post-processing)

`extract_jobs.py` joins `JobReport_C0.csv` (job boundaries) with `LatencyReport_C0.csv`
(per-access stages, filtered to `release_cycle ≤ Trace Cycle ≤ finish_cycle`), emitting the schema
the HTML already expects:

```json
{"job": j, "release_cycle": …, "finish_cycle": …, "n_accesses": …,
 "mean_lat_ns": mean(Total Latency)*CYCLE_NS, "max_lat_ns": max(Total Latency)*CYCLE_NS,
 "llc_misses": count(DRAM latency > 0), "deadline_miss": <from JobReport>}
```

One JSONL per act (`act1_alone`, `act2_contention`, `act3_rr`). `CYCLE_NS` calibration per plan §7.

---

## 7. Backward compatibility

- No `PERIOD` directive ⇒ legacy behavior, unchanged (all EEMBC/SPLASH traces unaffected).
- Periodic mode is per-CPU: core 0 runs the periodic task; cores 1–3 keep legacy streaming traces.
- `JobReport` is only written when `m_periodic` is true.

---

## 8. Config / CLI knobs

- Directives live in the workload file (portable with the trace). `OOO` overrides
  `m_number_of_OoO_requests` for the task; if absent, use the config/default.
- Everything else (arbiter FCFS/RR, protocol, buffers) is the usual `-s`/`-p` config — the three
  acts differ only by the bus arbiter (FCFS vs RR) and whether cores 1–3 are loaded.

---

## 9. Edge cases & fidelity notes (booth-honest)

- **Overrun cascade** — handled by `release = max(finish, jT)` + skipped-deadline counting.
- **Body with no accesses** — allowed; `finish = release + Σcompute`.
- **OoO>1** — `compute_time` is measured from the last *received* response, so with overlap it is
  an approximation; the control task should run `OOO=1`. Documented, not hidden.
- **Compute is a fixed budget**, not data-driven — correct for a *timing/interference* study.
- **Clock domains** — the task's compute is in CPU cycles; memory latency is returned in the same
  accounting the Logger already uses, so `finish - release` is coherent.

---

## 10. Open questions for sign-off

1. **Compute source.** Do we profile a specific real task (EKF localization? particle filter? A*
   / lattice planner?) for the compute:memory ratio, or start with a hand-set plausible budget and
   swap later? (Pipeline is identical either way.)
2. **Working-set size** for core 0 (how many cache lines) — small enough to hit when alone, and to
   be *evictable* by the contenders. Pick vs the LLC size (32 KB, 2-way here).
3. **Period `T`** — set so Act-1 jobs fit with margin and Act-2 jobs overrun. Chosen from the
   Act-1/Act-2 job-time distributions after the first runs (presentation tuning).
4. **`JobReport` in CPU vs Logger.** Spec puts the per-job row in the CPU (simplest). Alternative:
   have the `Logger` own it (it already tracks per-request completion). Preference?
5. **Number of jobs / run length** — enough jobs to fill one robot traversal start→goal at the
   chosen playback rate. Estimate once the period is set.

---

## 11. Prior art & positioning (why this design is standard, and what's novel)

The task model is deliberately aligned with established tools:

| Tool | Task model | We borrow |
|---|---|---|
| **Synopsys Platform Architect** (AMM/VPU, TGG) | task = compute cycles + read/write channels, WCET-characterized, mapped to VPUs; TGG extracts compute+accesses from traces | compute/communication split; WCET/profiled budgets; TGG idea (our `compute_time` = cycle-gap between accesses is a mini-TGG) |
| **gem5 traffic generator** (open) | state graph: `IDLE`/`LINEAR`/`RANDOM`/`TRACE`/`DRAM`, text config, data-agnostic | the `COMPUTE`+`LINEAR`/`RANDOM` vocabulary; generator rows for contenders |
| **SimSo** (open, RT scheduling) | task = {WCET, period, deadline} + scheduler; cache impact *statistical* | period/deadline attrs — but with *measured* execution time, not a WCET number |
| **Sesame / Daedalus** (open, MPSoC) | application (KPN) mapped to architecture, trace-driven co-sim | task→PE mapping (core 0 = RT task, cores 1–3 = aggressors) |

**Novelty of the Octopus version:** none of the above combines a **periodic RT task
(period/deadline)** with **cycle-accurate shared-memory *coherence* + *predictable arbitration*
(FCFS/RR/TDM)** interference. gem5 has the memory fidelity but no RT-task/deadline layer; SimSo has
tasks/deadlines but a statistical cache; Sesame abstracts memory. Octopus + this layer sits at that
intersection — RT task timing under real coherence/arbitration interference — which is exactly the
real-time-coherence story (and the ESSC/DATE angle).
