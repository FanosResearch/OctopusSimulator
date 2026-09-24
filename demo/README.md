# What this demo really does

A mobile robot follows a planned path. Its localization task runs periodically on core 0 of a
four‑core machine with a shared last‑level cache and one DDR4 controller. When three other cores
stream memory, the localization job finishes late, the robot steers on a stale waypoint and leaves
the path. Reserving one cache way for the task puts it back exactly where it was alone.

Every millisecond of that story is a cycle‑accurate Octopus run. Nothing about the timing is
modelled or tuned. This file explains precisely what is measured, what is drawn, and where the
line between the two is.

---

## 1. The chain, end to end

```
demo/workloads/act<N>/task_C0.task.csv        the periodic task (docs/Tasks.md)
        │                                      4 150 compute cycles + 20 accesses over 9 lines, period 5 000
        ▼  Octopus  (demo/run_acts.sh)
newLogger/JobReport_C0.csv                     one row per job: release, finish, deadline, miss, skipped
newLogger/LatencyReport_C0.csv                 one row per access: full stage breakdown
        │  demo/extract_jobs.py
        ▼
demo/data/act<N>.jsonl  (+ .manifest.json)     one record per job, the only thing the visuals read
        │
        ├─ demo/plot_trajectory.py  →  demo/figures/trajectory.pdf + demo/data/ate.csv
        └─ demo/build_page.py       →  demo/octopus-tracking-demo.html  (self-contained)
```

The robot's motion is a function of **two numbers per job**: the cycle the job was released and
the cycle it finished. Everything else on screen is derived from those.

## 2. A worked example

Act 2 (contention), job 121, exactly as Octopus wrote it:

```
job,release,finish,deadline,miss,accesses,skipped,mean_access_lat,max_access_lat
121,606539,787805,610000,1,20,35,8855,27848
```

The job ran 181 266 cycles inside a 5 000‑cycle period. It missed its deadline, and its overrun
swallowed 35 further periods for which no job could be released at all. The latency report
explains the number: its slowest access took 27 848 cycles, of which **27 832 were spent in DRAM**;
every bus and cache stage was 1 to 7 cycles. The arithmetic closes exactly:

| quantity | cycles |
|---|---|
| sum of the job's 20 access latencies | 177 116 |
| the task body's compute budget | 4 150 |
| **total** | **181 266** |
| job execution time Octopus reported | 181 266 |

Nothing is added on top of the simulation. A job's execution time *is* its memory latency plus its
fixed compute.

## 3. How a job becomes robot motion

The control rule, applied identically in the page and in the figure:

- Job *j* senses the pose at its **release** and plans the waypoint for one period later
  (`release + T`).
- That waypoint is applied only when the job **finishes**.
- The robot drives toward the current waypoint at a fixed speed cap.
- The error drawn is the distance to where the plan says the robot should be at that instant.

For job 121, the waypoint was computed for cycle 611 539 and applied at 787 805. The robot steered
for 176 000 cycles — 35 periods — on a waypoint that was already stale. One traversal of the path
is 200 periods, so that single job left the robot aiming at a point 17 % of a lap behind. That is
the long straight chord across the figure eight and the 6.6 m peak error in the table.

## 4. Measured versus chosen

| from Octopus (measured) | chosen for presentation |
|---|---|
| release, finish, deadline, miss, skipped periods | the path shape and its 12 m scale |
| every access latency and its stage breakdown | one traversal = 200 control periods |
| LLC misses, L1 hits, DRAM time per access | the robot's speed cap (1.55 × path speed) |
| which cores contended and how | the rule mapping a job to a waypoint |
| | the playback rate |

So the geometry is a visualisation and the timing is a measurement. The comparison between the
three acts is sound because they share identical geometry and differ **only** in simulated timing.
Acts 1 and 3 produce byte‑identical job reports, which is why their traces coincide exactly.

## 5. Why a nine‑line task suffers in a shared cache

Nine cache lines fit comfortably in a private L1, so the interference cannot be L1 capacity, and
the streaming cores are on *other* cores with their own L1s. The raw event trace
(docs/Trace.md) shows the actual mechanism. Over the whole act‑2 run, in core 0's L1:

| how the task lost a line | count |
|---|---|
| back‑invalidation from the shared LLC | 195 |
| the L1's own replacement | 0 |

On the LLC side, one FSM transition repeats exactly 195 times on the task's lines:

```
EorM --Own_Invalidation--> MN_d
```

The LLC is **inclusive**, so when it evicts a line it owns it must first invalidate the copy in the
owner's private L1. The shared LLC is 32 KB, 2‑way, hence 256 sets; the task's nine lines land in
sets 0–9, and the streamers sweep 256 KB and 4 MB regions that cover every set repeatedly. There
were 45 728 LLC replacements in that run, 195 of which threw out one of the task's nine lines — and
195 is precisely the number of core‑0 accesses that had to leave the L1.

| act | core‑0 accesses | served in its L1 | reached the LLC | went to DRAM |
|---|---|---|---|---|
| 1 (alone) | 4 000 | 3 991 | 9 | 9 |
| 2 (contention) | 1 140 | 945 | 195 | 195 |
| 3 (protected) | 4 000 | 3 991 | 9 | 9 |

The nine in acts 1 and 3 are the cold misses that first load the working set.

The second half of the damage is the memory controller. In act 2 the task's few DRAM reads wait far
longer than the streams that caused them:

| core | DRAM reads | mean wait | p99 | max |
|---|---|---|---|---|
| 0 (localization task) | 195 | 4 524 | 30 892 | 30 904 |
| 1 (one streamer) | 15 345 | 739 | 10 980 | 25 024 |

That is FR‑FCFS preferring the streams' row hits over an isolated read — the low‑intensity core is
penalised precisely because it is well behaved.

## 6. The three acts and their results

All three use the same pinned configuration the sweeps use: snoop MESI with exclusive L1s, a 32 KB
2‑way inclusive LLC, MCsim DDR4 with FR‑FCFS. They differ only in per‑run `-p` overrides.

| act | what differs | jobs released | missed or skipped periods | job time mean / worst | trajectory error (rmse / max) |
|---|---|---|---|---|---|
| 1 alone | cores 1–3 idle, FCFS bus | 200 | 0 | 4 193 / 4 747 | 0.13 m / 0.18 m |
| 2 contention | 3 streaming cores, FCFS bus | 57 | 198 | 19 717 / 181 266 | 4.13 m / 6.63 m |
| 3 protected | same load, RR bus + `way_partition=0:0` | 200 | 0 | 4 193 / 4 747 | 0.13 m / 0.18 m |

Act 3's protection is the LLC way reservation. Round‑Robin arbitration bounds the bus wait and is
the right thing to have, but on its own it does not help here: the damage is cache pollution and
DRAM queueing, not bus contention. Reserving one way stops the back‑invalidations, the L1 copy
survives, and the run becomes identical to running alone.

## 7. Running it

```bash
bash demo/run_acts.sh                 # simulate all three acts (in parallel, ~4 s), rebuild
                                      # demo/data, demo/figures/trajectory.pdf and the page
```

Then open `demo/octopus-tracking-demo.html`. Act buttons 1–3 replay one act; **Compare** shows all
three robots on one map and one shared clock.

To watch a run *as it simulates*:

```bash
bash demo/live.sh compare             # starts the three simulations and serves the page
bash demo/live.sh act2                # or just one
# open the printed URL, then press "Live"
```

The page polls `demo/live_server.py`, which tails each run's `JobReport_C0.csv` while Octopus
writes it. The view holds when it catches up with the simulator and loops once a run ends. The
**Live** button only appears when the page is loaded from that server, not from the file system.

Honest note on pacing: these acts finish in about four seconds of wall clock, while the page plays
a run at roughly 12 000 cycles per second. The records genuinely arrive during the run, but the
simulator finishes long before the view catches up. Live here means a feed played behind the write
head, not a simulation pacing the animation. Raise `attr,jobs` in the task files for a session that
stays live for minutes.

## 8. Files

| file | role |
|---|---|
| `tasks/localization_C0.task.csv` | the EKF‑style periodic task (the source of the per‑act copies) |
| `tasks/streamer_C1.task.csv` | the streaming aggressor |
| `workloads/act{1,2,3}/` | one workload directory per act, so the three can run at once |
| `run_acts.sh` | simulate the acts, extract, plot, rebuild the page |
| `extract_jobs.py` | JobReport + LatencyReport → per‑job JSONL + manifest |
| `plot_trajectory.py` | the paper‑style figure and the trajectory‑error table |
| `build_page.py` | embed the records so the page is self‑contained |
| `live_server.py`, `live.sh` | follow one or more runs while they simulate |
| `octopus-tracking-demo.html` | the page: single act, compare, or live |
| `octopus-demo-plan.md`, `periodic-task-spec.md`, `octopus-rt-demo-notes.md` | how this was designed and why |

## 9. What this demo does not claim

- The task's compute budget is a plausible EKF profile, not a profiled one. The memory behaviour
  is simulated; the compute is a fixed cycle budget (docs/Tasks.md).
- The robot kinematics are a deliberately simple steering rule, not a controller anyone would ship.
  They exist to make lateness visible, and they are applied identically to every act.
- The LLC is small relative to the streamers by design, which makes the effect sharp. A larger LLC
  or a less aggressive contender moves the threshold; with the streamers at 4 outstanding requests
  instead of 8, the task is barely touched. The collapse is a cliff, not a gradient.
- Deadlines are observed and reported, never enforced. A late job is not aborted.
