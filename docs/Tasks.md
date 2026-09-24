# Periodic tasks (`task_C<n>.task.csv`)

A core can run a **periodic real‑time task** instead of replaying an access trace: a fixed job
body (compute phases and memory accesses) re‑run every period, with the simulator itself
reporting each job's release, finish, deadline and miss. The memory system, protocols and
arbiters are untouched; the task layer lives in `CPU` (`CPU::periodicLogic`) and is opt‑in
per core. The legacy trace path is byte‑identical.

## Workload selection (per core)
For core `n` the system looks in the workload directory for, in order:
1. `task_C<n>.task.csv` — a periodic task program (this document);
2. `trace_C<n>.trc.shared` — an access trace (the classic path);
3. nothing — the core is **idle** (issues nothing, finishes at once). This is how "core 0 alone"
   configurations are expressed: give only core 0 a workload.

## Task program format
CSV, like every other Octopus input. Column 1 is the row **kind**; the rest are kind‑specific.
`#` lines, blank lines and the `kind,a,b,c,d,comment` header are ignored. Numbers accept hex
(`0x…`) or decimal.

```csv
kind,a,b,c,d,comment
attr,period,50000,,,control period in CPU cycles (= implicit deadline)
attr,jobs,200,,,number of jobs, then the core finishes   (-1 = free-running generator)
attr,ooo,1,,,outstanding-request window (1 = in-order core)
attr,deadline,45000,,,optional relative deadline (default: period)
COMPUTE,300,,,,advance 300 CPU cycles, no memory
READ,0x600040,,,,one read
WRITE,0x600000,,,,one write
LINEAR,0x600080,5,64,1.0,burst: base, count, stride, rd_ratio (0..1)
RANDOM,0x2000000,0x400000,1024,0.85,base, range, count, rd_ratio (line-aligned addresses)
```

| kind | a | b | c | d |
|---|---|---|---|---|
| `attr` | `period` / `jobs` / `ooo` / `deadline` / `start` / `base` | value | | |
| `COMPUTE` | cycles | | | |
| `READ` / `WRITE` | address | | | |
| `LINEAR` | base | count | stride | read ratio |
| `RANDOM` | base | range (bytes) | count | read ratio |

- Phase rows are the job body, top to bottom, replayed once per job. The `compute_time` of an
  access is the `COMPUTE` budget accumulated since the previous access (or the job start);
  `COMPUTE` after the last access is trailing compute that delays the job's finish.
- `LINEAR`/`RANDOM` draw their read/write choice (and `RANDOM` its line) from a per‑core
  deterministic generator, so runs are reproducible.
- `attr,base` is documentation only. `attr,ooo` overrides the CPU's configured window.
- `attr,start` (default 0) is the release cycle of job 0, i.e. the task's phase: all releases
  and deadlines are offset by it. Useful to start aggressors after a control task has warmed
  its working set, or to phase-shift several periodic tasks.
- `attr,jobs,-1` makes a **free‑running generator** (a memory aggressor): no period, no
  deadline, one body after another, stopping as soon as every core with a finite job count is
  done. At least one core must be finite, or the run never ends.

Worked examples: `demo/tasks/localization_C0.task.csv` (an EKF pose‑localization task: ~20
accesses over 9 lines and ~4 150 compute cycles per job) and `demo/tasks/streamer_C1.task.csv`
(a pure streaming aggressor).

## Execution semantics
With period `T`, relative deadline `D` (default `T`) and job index `j = 0, 1, …`, non‑preemptive,
one instance at a time:

```
release_j  = max(finish_{j-1}, j·T)
deadline_j = j·T + D
finish_j   = cycle the last access of the body has returned, plus trailing COMPUTE
miss_j     = finish_j > deadline_j
```

- The core idles from `finish_{j-1}` to `release_j` when a job finishes early.
- An overrun pushes the next release out (`release = finish`). Periods whose whole window
  `[kT, kT+T)` ended before the overrunning job finished are **skipped**: no job is released
  for them, and they are reported as `skipped_periods` on the overrunning job's row (each one
  is a missed deadline). Skipped periods consume job indices, so `jobs` counts periods.
- With `ooo = 1` accesses serialise: compute after the previous access's data, the clean
  compute↔memory sequencing of an in‑order control core. With `ooo > 1` the compute budget is
  counted from the last *returned* access, an approximation documented, not hidden.

## `JobReport_C<n>.csv`
Written by the Logger next to `LatencyReport_C<n>.csv`, one row per job:

```
job,release_cycle,finish_cycle,deadline_cycle,deadline_miss,n_accesses,skipped_periods,mean_access_lat,max_access_lat
```

The last two columns are the job's mean and worst access latency as the **core** observes it
(issue → response received), so a reader needs this file alone to show a job's timing; the stage
breakdown of each access stays in the latency report. Each row is flushed as the job finishes, so
the file can be tailed while the run is in progress — `demo/live_server.py` does exactly that.

Per‑access latencies and their stage decomposition stay in `LatencyReport_C<n>.csv`
(docs/Logger.md); a job's accesses are the rows with `Ready Cycle` in
`[release_cycle, finish_cycle]`. `demo/extract_jobs.py` joins the two for the demo.

## Protection: LLC way partitioning
Under a shared inclusive LLC, streaming cores evict a small periodic task's lines from the LLC
and, by inclusion, from its private L1 — the task then misses to DRAM on almost every access
(the localization example: 53 % of accesses to DRAM under three streamers, none alone). Bus
arbitration cannot help with that; **way reservation** can:

```
llc_controller.m_data_handler.way_partition(s),0:0;1-3:1
```
`"<cores>:<ways>;…"` with `a-b` ranges. A listed core's fills are installed in, and evict from,
its own ways only; cores not listed share the ways nobody claimed (so `0:0` alone reserves way 0
for core 0 and leaves the rest to everyone else). Implemented in `CacheDataHandler` (empty‑way
search and replacement candidate restricted to the requester's mask; the controller names the
requester while a fill is installed) with masked candidates in the LRU and Random policies.
Empty = no partitioning, byte‑identical to before. With `0:0` on a 2‑way LLC the example task
under full contention runs exactly as it does alone.

## What is and is not modelled
- Compute is a fixed cycle budget (Octopus has no data), so a task program is a *timing* model:
  right for interference studies, not for data‑dependent control flow.
- No preemption and no multi‑task scheduling on a core: one periodic task per core.
- Deadlines are observed and reported, never enforced (a late job is not aborted).
