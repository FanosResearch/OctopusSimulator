# 01 — Exploration: one axis at a time

**Goal:** convince yourself the axes really are independent. Change one thing,
re-run (a few seconds), watch one number move. About 25 minutes.

## 1. Save each arbiter setting as its own run

Run these commands from the **project root**, using the shipped configuration
from exercise 00. Each invocation starts from that same CSV baseline: `-p`
overrides apply only to that run, not to the next command.

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -o tutorial/01-exploration/output/Arbiter/FCFS --trace

./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter" \
  -o tutorial/01-exploration/output/Arbiter/RR --trace

./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter" \
  -o tutorial/01-exploration/output/Arbiter/TDM --trace
```

`-o` creates each setting's directory. Its CSVs and trace stay together, so there
is no need to copy reports before the next run. Reusing the same output path
replaces that setting's reports. `output/` is Git-ignored.

## 2. Compare the saved settings

```shell
python3 sweeps/plot_axis.py tutorial/01-exploration/output/Arbiter
./octoviz.sh serve tutorial/01-exploration/output/Arbiter
```

The plotter reads each setting's `Summary.csv` and writes PNG/PDF charts and
`metrics.csv` under `Arbiter/figures/`. It requires matplotlib and NumPy. The
comparison chart shows finish cycle, mean effective latency, and worst-case total
latency. The stage chart shows where the worst delays occur. Stage maxima are
independent; do not add them to obtain worst-case total latency.

Octoviz automatically converts missing visualization files and lets you select
FCFS, RR, or TDM. Neither command reruns the simulator. If you rerun a setting
that has already been converted, refresh it explicitly with
`./octoviz.sh convert <setting-directory>` before viewing it again.

Compare these values (worst cases and finish are maxima across **all cores**,
not just the first row of `Summary.csv`):

| arbiter | worst total | worst request bus | worst response bus | finish cycle |
|---|---|---|---|---|
| FCFS | | | | |
| RR | | | | |
| TDM | | | | |

Which stages change most? Does the setting with the lowest worst-case latency
also finish first? Use the timeline to investigate the differences.

## 3. Explore another axis

Use the same layout, `output/<axis>/<setting>/`, for the other experiments. Three possible axes you can explore are `Memory` simulation, `MSHR` size, and cache `Partition`.

For whichever axis you choose, also record the unmodified baseline in that axis's
`Baseline/` folder. For example, for `Memory`:

```shell
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -o tutorial/01-exploration/output/Memory/Baseline --trace
python3 sweeps/plot_axis.py tutorial/01-exploration/output/Memory
```

For MSHR or partitioning, use `MSHR/Baseline` or `Partition/Baseline` instead and
plot that axis directory. Keep the benchmark and all unrelated settings fixed.
The memory experiment selects the memory model and its scheduler together;
these commands are independent variations of the baseline, not cumulative edits.

### Full DRAM simulation with MCSim (FRFCFS arbiter)
```shell
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "main_memory_type(s)=MCsim" -p "mcsim_scheduler(s)=FRFCFS" \
  -o tutorial/01-exploration/output/Memory/MCsim --trace
```
_Baseline value: Fixed-latency model (`MainMemoryController`)_

### MSHR size 4
```shell
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "cache_controller[*].num_mshr(i)=4" \
  -o tutorial/01-exploration/output/MSHR/4 --trace
```
_Baseline value: `16`_

### Way partitioning
```shell
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "llc_controller.m_data_handler.way_partition(s)=0:0;1-3:1" \
  -o tutorial/01-exploration/output/Partition/Reserved --trace
```
_Baseline value: No partitioning_

Having generated the baseline statistics and modified experiment's
 statistics in a given axis's folder, try graphing and visualizing the
 results as before. See if you can identify the different setting's
 effect on the timeline view in the visualizer and the overall trend.


## The axes that ship

| axis | parameter | values |
|---|---|---|
| bus arbiter | `bus[0].interconnect_controller.arbiter_type` | `FCFSArbiter` `RRArbiter` `TDMArbiter` |
| LLC arbiter | `llc_controller.arbiter_type` | `FCFSArbiter` `RRArbiter` |
| main memory | `main_memory_type` | `MainMemoryController` (fixed latency) `MCsim` (cycle-accurate DDR4) |
| DRAM scheduler | `mcsim_scheduler` | `FRFCFS` `FCFS` `BLISS` `AMC` `MAG` … (`src/MCsim/system/`) |
| LLC way partition | `llc_controller.m_data_handler.way_partition` | e.g. `0:0;1-3:1` |
| queues | `*.num_mshr` `*.pwb_size` `*.processing_queue_size` | integers, `-1` = unbounded |
| **coherence protocol** | **a whole preset — see below** | snoop MSI · snoop MESI · directory MSI |

## Switching protocol — not a `-p`

A protocol is five coupled parameters (controller type, L1 and LLC `protocol_type`,
L1 and LLC `fsm_filename`). Overriding some of them on the command line segfaults.
Switch protocol by selecting a whole preset instead:

```shell
./run_octopus.sh --protocol snoop     --suite eembc --bench a2time01-trace   # snoop MSI
./run_octopus.sh --protocol directory --suite eembc --bench a2time01-trace   # directory MSI, FCFS bus
git checkout -- configuration/      # back to snoop MESI as shipped
```

`run_octopus.sh` copies the preset over `MultiCoreSystem.csv` — that is why the
reset command exists. Its summary line (`PASS: 1 / 1 complete`) uses a strict test:
every core drained its own trace and wrote the end-of-simulation footer.

## If you finish early

Repeat the arbiter comparison on `cacheb01-trace` — the benchmark where all four
cores fight over one block. Save these in a separate axis folder such as `output/Arbiter-cacheb01/` so the
`a2time01` results remain available. Compare how strongly arbitration affects
each benchmark.

`sweeps/` holds the scripts that sweep these axes in bulk and produce the figures in
the papers; `sweep_protocols.sh` is the readable entry point.
