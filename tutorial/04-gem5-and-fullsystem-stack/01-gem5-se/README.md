# 04/01 — gem5 + Octopus in SE mode

**Goal:** run real programs on gem5's out-of-order cores with Octopus as their
memory hierarchy. First a self-checking test that shows the interface works,
then a small real-time SLAM that shows what memory interference does to a
real-time task, and what the hierarchy's knobs can do about it.

| part | what | time |
|---|---|---|
| A | `se_test`: one SE run, then the same run with a different bus arbiter | 5 min |
| B | two views of one run: gem5's statistics next to Octopus's own reports | 5 min |
| C | the interference demo: four configurations, one per core, then the plots | 10 min (runs in the background) |

## The journey of a memory request

1. The gem5 CPU model issues a load or store.
2. It crosses into Octopus through `ExternalCPU` (`header/ExternalCPU.h`), a
   `CommunicationInterface` producer exactly like the trace-driven `CPU` you used
   in exercises 00–03. Nothing downstream can tell the difference.
3. From there it is an ordinary Octopus `Message`: L1 controller, bus
   arbitration, LLC, and out to memory.
4. Memory is `MCsimInterface`: the address is handed to MCsim's DDR4 model and
   the fill comes back through a callback.
5. The response travels back the same way and completes the load in the gem5
   core, with real data: under gem5 the bytes matter, not only the timing
   (`docs/StateAndData.md`).

Steps 2–4 are **the same code** you ran standalone. Only the request source
changed. The gem5 side is `gem5/octopus.cc` (a SimObject per L1) and
`gem5/configs/octopus_cache_hierarchy.py`; the system is the CSV preset
`configuration/SystemConfigurations/MultiCoreSystem_gem5.csv` (4 cores, 8 L1s:
instruction caches are Octopus ids 0–3, data caches 4–7, the LLC is 10).

## Step 1 — set up

> **In a codespace or the dev container, there is nothing to set up:** `gem5`
> is on the `PATH` and the two workloads are prebuilt. Run `check.sh` (below)
> and go on to Part A.

Commands run from the repository root unless they `cd`. Outside the container,
build gem5 first (folder README, `../README.md`, "gem5 outside the container").

The two aarch64 binaries come prebuilt (`gem5/se_test/se_test-static` and
`slam_demo/slam_demo`; static, so gem5 SE needs no disk image), and `expected/`
was produced with exactly these. Rebuild them only after changing their source
(skip this otherwise):

```shell
make -C gem5/se_test
make -C tutorial/04-gem5-and-fullsystem-stack/01-gem5-se/slam_demo
```

`bash tutorial/04-gem5-and-fullsystem-stack/01-gem5-se/check.sh` runs Parts A and B
and the demo's Solo configuration (about 3 minutes) and tells you whether
everything works.

## Part A — `se_test`

```shell
gem5 -re -d m5out_se gem5/configs/se_arm.py
tail -5 m5out_se/simout.txt          # RESULT: PASS
```

`se_test` (`gem5/se_test/se_test.cpp`) runs 4 threads through three phases, each
aimed at a path the hierarchy must get right: private streaming (misses,
write-backs, LLC refills), one shared atomic counter (a line moving between
cores on every increment) and producer/consumer slices (invalidations and
cache-to-cache data). Every phase checks its own result.

Now change one thing, the L1↔LLC bus arbiter, from the command line:

```shell
gem5 -re -d m5out_se_rr gem5/configs/se_arm.py \
    --octopus-param 'bus[0].interconnect_controller.arbiter_type(s)=RRArbiter'
grep simTicks m5out_se/stats.txt m5out_se_rr/stats.txt
```

`--octopus-param` takes any `name(type)=value` line of the preset and may be
repeated; nothing is rebuilt. The tick counts are close: `se_test`'s threads are
symmetric, so no core is squeezed out under either arbiter. Part C is a
workload where the arbiter matters.

## Part B — two views of one run

A gem5 run has two sets of books: gem5's `stats.txt`, and Octopus's own reports,
the `newLogger/` you read in exercise 00 (`LatencyReport_C<n>.csv`, one row per
request; `Summary.csv`, worst cases per core). Octopus writes them under gem5
too when asked:

```shell
gem5 -re -d m5out_views gem5/configs/se_arm.py \
    --octopus-param 'cpu[*].log_requests(i)=1'
bash tutorial/04-gem5-and-fullsystem-stack/01-gem5-se/compare_views.sh m5out_views
```

- **Same window.** Octopus logging follows gem5's statistics: a stats reset
  (`se_test`'s work-begin marker, `m5 resetstats`) starts a fresh log, the next
  stats dump writes `newLogger/`. Both cover the region of interest only.
- **Per core.** A core's instruction and data L1 report into one
  `LatencyReport_C<core>.csv`, as a trace-driven core does in exercise 00.
- **gem5's side** is the bridge's counters in `stats.txt`, per L1:
  `board.cache_hierarchy.l1d_caches<n>.readReqs / writeReqs / responses /
  avgLatency`.

`compare_views.sh` puts them side by side (reference: `expected/two_views.txt`):

```
core   gem5 requests   Octopus rows in flight       gem5 latency  Octopus latency
0             414175         414172         3            96.7 cy          95.7 cy
1             411189         411189         0            98.1 cy          97.1 cy
2             409017         409017         0            99.0 cy          98.0 cy
3             408568         408568         0            98.4 cy          97.4 cy
```

Every packet gem5 hands to the L1 is one Octopus request, so the counts are
equal; the few missing rows (core 0) are requests still in flight when the
window closed. The latencies agree to about a cycle: gem5 measures from the
bridge's hand-off to its response, Octopus from the CPU issue to the response
(`Total Latency`). The rest of exercise 00 applies unchanged: read a row's
stages in `LatencyReport`, the worst cases in `Summary.csv`. With
`OCTOPUS_TRACE=<file>` set as well, the run also writes the raw event trace
(`docs/Trace.md`). The reports are large (a row per request, about 27 MB per
core here), so logging is off unless you ask for it.

## Part C — a real-time SLAM under memory interference

The idea follows Bechtel & Yun, *Analysis and Mitigation of Shared Resource
Contention on Heterogeneous Multicore* (ARM Industrial Challenge 2022): a SLAM
has to keep up with a sensor while a co-runner, sharing only the memory system,
slows it down; accuracy against ground truth is the end metric.

### The workload (`slam_demo/slam_demo.cpp`)

A 2D lidar SLAM (Hector-style scan matching on an occupancy grid) drives 20
scans along a corridor of an indoor loop. Four threads, one per core:

| core | thread | does |
|---|---|---|
| 0 | player | releases scan *k* at *k* × 80 µs of simulated time (the sensor) |
| 1 | aggressor (optional) | sweeps a private buffer, one access per 64-byte line |
| 2 | mapper | writes every 2nd scan (a *keyframe*) into the shared grid |
| 3 | front-end | takes the newest scan, guesses the pose from the last motion, matches the scan against the grid |

Timing becomes error in two ways, as in the paper:

- a late **front-end drops scans**: a newer one has arrived, so the older one is
  skipped and the next guess has to bridge a larger gap;
- a late **mapper leaves the map stale**: the robot matches against a map that
  does not yet contain the area it is entering, and the error it picks up is
  written into the map by the next keyframe.

What the demo reports:

- **dropped**: scans the front-end never processed;
- **lost**: scans whose match it discarded (too few beams near a mapped wall, or
  a jump over 1 m) and replaced by the guess;
- **keyframes mapped**: how many keyframes the mapper wrote before the end;
- **position error**: per scan, the distance between the estimated and the true
  position *at that scan*; the RMSE over the run is the headline (ATE).

`python tutorial/04-gem5-and-fullsystem-stack/01-gem5-se/slam_demo/viz/slam_steps.py`
draws the algorithm step by step (scan, grid, matching, keyframes, timeline).

### The configurations (`run_matrix.sh`)

Two aggressors, each hurting the SLAM in its own way. Round-robin arbitration
answers the first and not the second; way partitioning answers the second.

| config | aggressor | contends for | Octopus options |
|---|---|---|---|
| `L_fcfs` | **light**: 2 MiB write sweep, fits the LLC | bus and LLC queues | |
| `L_rr` | light | | round-robin bus and LLC arbitration |
| `H_rr` | **heavy**: 16 MiB read sweep, twice the LLC | LLC capacity: evictions, and back-invalidations of the SLAM's L1 lines | round-robin bus and LLC arbitration |
| `H_part` | heavy | | LLC way partitioning: the aggressor fills only way 0 |

```shell
cd tutorial/04-gem5-and-fullsystem-stack/01-gem5-se
bash run_matrix.sh                   # the four in parallel, one per core
python slam_demo/viz/plot_matrix.py  # figures/ from runs/
```

The plots compare against the SLAM alone, the run `check.sh` makes; its
reference is `expected/runs/A_solo` (`bash run_matrix.sh A_solo` reruns it).

The partition `way_partition(s)=1:0;5:0` names Octopus requester ids: gem5 SE
gives each new thread the next free core, the aggressor thread is created first
and so runs on core 1, whose L1s are ids 1 and 5.

### What you should see

Reference results (`expected/summary.txt`, figures in `expected/figures/`):

| config | dropped | keyframes mapped | front-end µs, median / max | mapper µs per keyframe, median / max | error RMSE | aggressor |
|---|---|---|---|---|---|---|
| Solo | 0 | 9/9 | 53.5 / 56.1 | 145 / 150 | 4.2 cm | |
| light | 0 | 9/9 | 60.4 / 64.6 | 154 / 183 | 6.7 cm | 19.3 GB/s |
| light + RR | 0 | 9/9 | 53.4 / 55.8 | 145 / 149 | 4.2 cm | 19.2 GB/s |
| heavy + RR | 6 | 1/6 | 76 / 458 | 1414 | *failed* | 9.2 GB/s |
| heavy + partition | 0 | 9/9 | 54.1 / 56.4 | 147 / 149 | 6.7 cm | 8.1 GB/s |

The working configurations reproduce closely. The failed one varies from run
to run: once the map stops growing, small timing differences decide where the
estimate drifts.

- The **light** aggressor slows both SLAM threads (front-end +13 %, the mapper's
  worst keyframe +22 %) and the error rises by 60 % without a single dropped
  scan: the stale-map path. **Round-robin** arbitration undoes it completely,
  and the aggressor keeps its full bandwidth: under FCFS the SLAM's requests
  queue behind the aggressor's; under round-robin each requester takes its
  turn.
- The **heavy** aggressor moves less data (it waits on DRAM) but evicts the
  SLAM's lines from the inclusive LLC, and with them from its L1s. The mapper
  slows down tenfold and writes 1 keyframe; the front-end drops scans and
  matches against a map that ends near the start: tracking fails, round-robin
  or not, since nothing is queueing for it to reorder. **Way partitioning**
  keeps the aggressor out of the SLAM's ways and tracking is back, the
  aggressor still at 8.1 GB/s.

### Where the time goes: Octopus's own reports

The figures above are the program's view. The hierarchy's view says *why*:
rerun the matrix with Octopus's per-request reports (Part B), reduce them, and
plot again.

```shell
LOG=1 bash run_matrix.sh                       # about 300 MB of reports per run, 2x the time
for d in runs/*/; do bash slam_demo/viz/reduce_reports.sh $d --delete; done
python slam_demo/viz/plot_matrix.py            # adds figures/5_memory_breakdown.png
```

`reduce_reports.sh` keeps, per core, the requests that left the L1 (the
spinning threads hit their L1 millions of times, which would drown the rest)
and their mean time per `LatencyReport` stage, in `runs/<config>/breakdown.csv`
(reference: `expected/runs/<config>/breakdown.csv`,
`expected/figures/5_memory_breakdown.png`). Logging does not change the
simulation: the logged runs give the same numbers.

| config | front-end core: cycles per request past the L1 | of which | reached DRAM |
|---|---|---|---|
| Solo | 15 | response bus 5 | 209 |
| light | 147 | **response bus 130** | 202 |
| light + RR | 19 | response bus 8 | 207 |
| heavy + RR | 1952 | **DRAM 1849**, LLC queue 85 | **1450** |
| heavy + partition | 101 | DRAM 85 | 207 |

- **Light aggressor:** the SLAM's requests wait for the response bus behind
  the aggressor's stream of LLC refills, about 25 times longer than alone.
  Round-robin gives each requester its turn, and the wait is back to Solo.
- **Heavy aggressor:** seven times more of the SLAM's requests reach DRAM (its
  lines were evicted from the LLC), and each waits there behind the
  aggressor's misses. Partitioning brings the count of DRAM requests back to
  Solo; the few left still queue behind the aggressor at DRAM, which is the
  DRAM scheduler's job (see "Going further").

### Reading the error

The error is meaningful only when tracking worked. Decide that first, from
numbers that do not depend on the error:

| | tracking worked | tracking failed |
|---|---|---|
| dropped scans | none or a few | many |
| keyframes mapped | all (or all but the last) | a few |
| mapper time per keyframe | within 2 periods | far beyond |
| error over the run (figure 3) | flat, a few cm | grows to metres |

When it worked, compare RMSE between configurations. A few centimetres are
within what timing alone moves in a run this short (the front-end reads a grid
the mapper is writing, and which updates it sees depends on timing), so small
differences need several scenarios before they mean anything. When it failed,
the size of the error only says where the estimate happened to drift: report
the failure (dropped, keyframes mapped) and look at the trajectories.

## Things to know about SE mode

- No scheduler: every thread needs a core of its own (`--num-cores`), and a
  waiting thread spins. The demo uses 4 threads on 4 cores.
- `clock_gettime` follows simulated time, which is what lets the player release
  scans on a real-time period.
- gem5 is deterministic: the same gem5 build, the same program binary and the
  same options give the same numbers, so a difference between two runs is the
  change you made. Rebuilding the program (even from the same source, at
  another path) moves its code and data in memory and shifts timing slightly,
  which is why your numbers can differ from `expected/` in the last digits.

## Going further

- The DRAM scheduler is a third knob: `--octopus-param 'mcsim_scheduler(s)=BLISS'`
  (any directory under `src/MCsim/system/`). It matters only when the SLAM's own
  requests reach DRAM, i.e. with the heavy aggressor and no partition.
