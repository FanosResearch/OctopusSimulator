# 04/01 — gem5 + Octopus in SE mode

**Goal:** run real programs on gem5's out-of-order cores with Octopus as their
memory hierarchy. First a self-checking test that shows the interface works,
then a small real-time SLAM that shows what memory interference does to a
real-time task, and what the hierarchy's knobs can do about it.

| part | what | time |
|---|---|---|
| A | `se_test`: one SE run, then the same run with a different bus arbiter | 5 min |
| B | the interference demo: seven configurations, then the plots | 15 min (runs in the background) |

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
instruction caches are Octopus ids 0–3, data caches 4–7, the LLC is 10). Under
gem5, Octopus's own `LatencyReport` files are not written; the measurements come
from gem5's `stats.txt` and from the programs themselves.

## Step 1 — build

```shell
export GEM5_ROOT=<your gem5 checkout>
# gem5 with this repository linked in (once; minutes):
(cd $GEM5_ROOT && scons EXTRAS=<this repository> build/ARM/gem5.opt -j$(nproc))

make -C gem5/se_test                     # the self-checking SE test (aarch64, static)
cd tutorial/04-gem5-and-fullsystem-stack/01-gem5-se
make -C slam_demo                        # the demo (aarch64, static)
make -C slam_demo steps                  # scenario data for the plots
```

The aarch64 binaries need `g++-aarch64-linux-gnu`; the plots need Python with
`numpy` and `matplotlib`. Both binaries are static, so gem5 SE needs no disk image.

`bash check.sh` (in this folder) runs Part A and the demo's Solo configuration
(about 5 minutes) and tells you whether everything works.

## Part A — `se_test`

```shell
$GEM5_ROOT/build/ARM/gem5.opt -re -d m5out_se gem5/configs/se_arm.py
tail -5 m5out_se/simout.txt          # RESULT: PASS
```

`se_test` (`gem5/se_test/se_test.cpp`) runs 4 threads through three phases, each
aimed at a path the hierarchy must get right: private streaming (misses,
write-backs, LLC refills), one shared atomic counter (a line moving between
cores on every increment) and producer/consumer slices (invalidations and
cache-to-cache data). Every phase checks its own result.

Now change one thing, the L1↔LLC bus arbiter, from the command line:

```shell
$GEM5_ROOT/build/ARM/gem5.opt -re -d m5out_se_rr gem5/configs/se_arm.py \
    --octopus-param 'bus[0].interconnect_controller.arbiter_type(s)=RRArbiter'
grep simTicks m5out_se/stats.txt m5out_se_rr/stats.txt
```

`--octopus-param` takes any `name(type)=value` line of the preset and may be
repeated; nothing is rebuilt. The tick counts are close: `se_test`'s threads are
symmetric, so no core is squeezed out under either arbiter. Part B is a
workload where the arbiter matters.

## Part B — a real-time SLAM under memory interference

The idea follows Bechtel & Yun, *Analysis and Mitigation of Shared Resource
Contention on Heterogeneous Multicore* (ARM Industrial Challenge 2022): a SLAM
has to keep up with a sensor while a co-runner, sharing only the memory system,
slows it down; accuracy against ground truth is the end metric.

### The workload (`slam_demo/slam_demo.cpp`)

A 2D lidar SLAM (Hector-style scan matching on an occupancy grid) drives 40
scans through a corner of a corridor loop. Four threads, one per core:

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

`python slam_demo/viz/slam_steps.py` draws the algorithm step by step (scan,
grid, matching, keyframes, timeline) from a run on this machine.

### The configurations (`run_matrix.sh`)

| config | aggressor | contends for | Octopus options |
|---|---|---|---|
| `A_solo` | none | | |
| `L_fcfs` | **light**: 2 MiB write sweep, fits the LLC | bus and LLC queues | |
| `L_rr` | light | | round-robin bus and LLC arbitration |
| `H_fcfs` | **heavy**: 16 MiB read sweep, twice the LLC | LLC capacity: evictions, and back-invalidations of the SLAM's L1 lines | |
| `H_rr` | heavy | | round-robin |
| `H_part` | heavy | | LLC way partitioning: the aggressor fills only way 0 |
| `H_rr_part` | heavy | | both |

```shell
bash run_matrix.sh                   # all seven in parallel, ~10 min on 7+ cores
python slam_demo/viz/plot_matrix.py  # figures/ from runs/
```

The partition `way_partition(s)=1:0;5:0` names Octopus requester ids: gem5 SE
gives each new thread the next free core, the aggressor thread is created first
and so runs on core 1, whose L1s are ids 1 and 5.

### What you should see

Reference results (`expected/summary.txt`, figures in `expected/figures/`):

| config | dropped | keyframes mapped | front-end µs, median / max | mapper µs per keyframe, median / max | error RMSE | aggressor |
|---|---|---|---|---|---|---|
| Solo | 0 | 19/19 | 54.5 / 57.8 | 143 / 150 | 5.4 cm | |
| light | 0 | 19/19 | 61.5 / 71.3 | 152 / 184 | 8.7 cm | 19.2 GB/s |
| light + RR | 0 | 19/19 | 54.5 / 58.1 | 143 / 150 | 5.5 cm | 19.2 GB/s |
| heavy | 8 | 2/15 | 71 / 422 | 1555 | *failed* | 8.8 GB/s |
| heavy + RR | 9 | 2/15 | 79 / 422 | 1505 | *failed* | 8.8 GB/s |
| heavy + partition | 0 | 19/19 | 55.0 / 85.4 | 143 / 150 | 9.7 cm | 8.1 GB/s |
| heavy + RR + partition | 0 | 19/19 | 54.9 / 85.4 | 143 / 150 | 9.5 cm | 8.1 GB/s |

Your numbers for the working configurations should match these closely. The
two failed ones vary from build to build (in our runs, 6 to 11 dropped scans,
2 of 14–16 keyframes, errors from 2 to 12 m): once the map stops growing, tiny
timing differences decide where the estimate drifts.

- The **light** aggressor slows both SLAM threads (front-end +13 %, the mapper's
  worst keyframe +23 %) and the error rises by 60 % without a single dropped
  scan: the stale-map path. **Round-robin** arbitration undoes it completely,
  and the aggressor keeps its full bandwidth: under FCFS the SLAM's requests
  queued behind the aggressor's; under round-robin each requester takes its
  turn.
- The **heavy** aggressor moves less data (it waits on DRAM) but evicts the
  SLAM's lines from the inclusive LLC, and with them from its L1s. The mapper
  slows down tenfold and writes 2 keyframes; the front-end drops scans and
  matches against a map that ends near the start: tracking fails. Round-robin
  cannot help, nothing is queueing. **Way partitioning** keeps the aggressor
  out of the SLAM's ways and tracking is back, the aggressor still at 8.1 GB/s.

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

## What you will *not* be able to do here

Change Octopus C++ and see it under gem5 without relinking `gem5.opt` (see the
parent README).

## Going further

- The DRAM scheduler is a third knob: `--octopus-param 'mcsim_scheduler(s)=BLISS'`
  (any directory under `src/MCsim/system/`). It matters only when the SLAM's own
  requests reach DRAM, i.e. with the heavy aggressor and no partition.
- `docs/StateAndData.md` explains what changes once a data array has a latency
  and real data flows, and why `MultiCoreSystem_gem5.csv` sets `line_interlock`
  and a pipelined LLC array.
