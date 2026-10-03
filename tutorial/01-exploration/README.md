# 01 — Exploration: one axis at a time

**Goal:** convince yourself the axes really are independent. Change one thing,
re-run (a few seconds), watch one number move. About 25 minutes.

## 1. Save each arbiter setting as its own run

Run these commands from the **project root**, using the shipped configuration
from exercise 00. Each invocation starts from that same CSV baseline: `-p`
overrides apply only to that run, not to the next command.
`-o` creates each setting's output directory for the statistics and visualizer data.

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

# First-come-first-serve arbiter
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -o tutorial/01-exploration/output/Arbiter/FCFS --trace

# Round-robin arbiter
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter" \
  -o tutorial/01-exploration/output/Arbiter/RR --trace

# Time division multiplexed arbiter
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter" \
  -o tutorial/01-exploration/output/Arbiter/TDM --trace
```


## 2. Compare the saved settings

```shell
python3 sweeps/plot_axis.py tutorial/01-exploration/output/Arbiter
./octoviz.sh serve tutorial/01-exploration/output/Arbiter
```

The plotter reads each setting's `Summary.csv` and writes PNG/PDF charts and
`metrics.csv` under `Arbiter/figures/`.  The
comparison chart shows finish cycle, mean effective latency, and worst-case total
latency. The stage chart shows where the worst delays occur. Stage maxima are
independent; do not add them to obtain worst-case total latency.

Octoviz automatically converts missing visualization files and lets you select
FCFS, RR, or TDM. Neither command reruns the simulator. If you rerun a setting
that has already been converted, refresh it explicitly with
`./octoviz.sh convert <setting-directory>` before viewing it again.

Open each setting's `Summary_transposed.csv` to compare these values. Worst cases
and finish are maxima across **all core columns**, not just Core 0:

| arbiter | worst total | worst request bus | worst response bus | finish cycle |
|---|---|---|---|---|
| FCFS | | | | |
| RR | | | | |
| TDM | | | | |

Which stages change most? Does the setting with the lowest worst-case latency
also finish first? Use the timeline to investigate the differences.

## 3. Explore another axis

Use the same layout, `output/<axis>/<setting>/`, for the other experiments. Two other possible axes you can explore are `Memory` simulation and cache `Partition`.

For whichever axis you choose, also record the unmodified baseline in that axis's
`Baseline/` folder. Keep the benchmark and all unrelated settings fixed.
The memory experiment selects the memory model and its scheduler together;
these commands are independent variations of the baseline, not cumulative edits.

### Full DRAM simulation with MCSim (FRFCFS arbiter)
```shell
# Baseline: fixed-latency memory
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -o tutorial/01-exploration/output/Memory/Baseline --trace

# Full DRAM simulation
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "main_memory_type(s)=MCsim" -p "mcsim_scheduler(s)=FRFCFS" \
  -o tutorial/01-exploration/output/Memory/MCsim --trace
```
_Baseline value: Fixed-latency model (`MainMemoryController`)_

### Way partitioning
```shell
# Baseline: no partitioning
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -o tutorial/01-exploration/output/Partition/Baseline --trace

# Reserve LLC ways
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
  -p "llc_controller.m_data_handler.way_partition(s)=0:0;1-3:1" \
  -o tutorial/01-exploration/output/Partition/Reserved --trace
```
_Baseline value: No partitioning_

Having generated the baseline statistics and modified experiment's
 statistics in a given axis's folder, try graphing and visualizing the
 results as before. See if you can identify the different setting's
 effect on the timeline view in the visualizer and the overall trend.

Way reservation prevents a streaming core's LLC fills from evicting a quiet core's
lines and, by inclusion, its L1 copies. The sequence is drawn in
[`docs/imgs/inclusion_interference.svg`](../../docs/imgs/inclusion_interference.svg).
Compare the reserved-way run with its baseline: does it reduce the quiet core's
DRAM traffic and worst-case latency?

## The axes that ship

| axis | parameter | values |
|---|---|---|
| bus arbiter | `bus[0].interconnect_controller.arbiter_type` | `FCFSArbiter` `RRArbiter` `TDMArbiter` |
| LLC arbiter | `llc_controller.arbiter_type` | `FCFSArbiter` `RRArbiter` |
| main memory | `main_memory_type` | `MainMemoryController` (fixed latency) `MCsim` (cycle-accurate DDR4) |
| DRAM scheduler | `mcsim_scheduler` | `FRFCFS` `FCFS` `BLISS` `AMC` `MAG` … (`src/MCsim/system/`) |
| LLC way partition | `llc_controller.m_data_handler.way_partition` | e.g. `0:0;1-3:1` |
| queues | `*.num_mshr` `*.pwb_size` `*.processing_queue_size` | integers, `-1` = unbounded |
| **coherence protocol** | **system preset (`-c`) or coordinated overrides — see below** | snoop MSI · snoop MESI · directory MSI |

## Switching coherence families with a system preset

Switching between snooping and directory coherence requires coordinated controller,
protocol, and FSM settings. The supplied system CSVs group those choices into
presets. Select one directly with `-c` (or `--config`):

```shell
# Run from the project root, using the same benchmark for both presets.
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -o tutorial/01-exploration/output/Coherence/Snoop-MSI --trace

./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Directory \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -o tutorial/01-exploration/output/Coherence/Directory-MSI --trace
```

`-s` still selects the C++ system class and its wiring. `-c` selects the system
CSV from `configuration/SystemConfigurations/`; the `.csv` suffix is optional.
You can also supply a path, such as `--config ./my-system.csv`. `-p` overrides
apply after loading the selected CSV. No files are copied or edited, so there is
no configuration reset step afterward. Omitting `-c` uses `MultiCoreSystem.csv`
again, with whatever settings it currently contains.

Both presets above select **MSI**. FCFS is pinned for both runs because the
directory preset is validated with FCFS and documents a starvation issue with
TDM. The presets also differ in buffer and queue sizing: this is a comparison of
complete configurations, not an isolated change to one protocol parameter.

Graph the data and observe the trend:

```shell
python3 sweeps/plot_axis.py tutorial/01-exploration/output/Coherence
```

### Changing MSI/MESI within a family

A preset is convenient, but protocol selection is not restricted to presets.
Multiple `-p` overrides can change the matched L1/LLC protocol and FSM settings.
For example, starting from the snoop MSI preset, this selects snoop MESI:

```shell
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cache_controller_type(s)=CacheControllerExclusive" \
  -p "cache_controller[*].protocol_type(s)=SNOOP_MESI" \
  -p "cache_controller[*].fsm_filename(s)=MESI_splitBus_snooping" \
  -p "llc_controller.protocol_type(s)=SNOOP_LLC_MESI" \
  -p "llc_controller.fsm_filename(s)=MESI_LLC" \
  -o tutorial/01-exploration/output/Coherence/Snoop-MESI --trace
```


Snooping MESI needs the exclusive-capable L1 controller as well as the two
protocol names and two FSM filenames. Changing just a name can leave an
incompatible combination. Rerun the plotter to include the new setting and
restart `serve` to discover and convert it.

### Shared bus vs two network layouts
The base `MultiCoreSystem` class provides for a shared bus connecting
the cache controllers. Within this class, we selected either a
snooping- or directory-based coherence protocol by specifying a different
configuration preset CSV file with `-c`. 

This version of Octopus also provides a class that allows for a simple
network on chip (NoC). We can specify it by passing 
`-s MultiCoreSystem_Mesh` as the command line argument to Octopus.


There are two preset network topologies provided: 
- a star layout (`configuration/Interconnect/NoC.csv`), in
which all L1 caches are connected directly to the LLC and DRAM directly,
- and a mesh layout (`configuration/Interconnect/Mesh.csv`) in which all cache controllers are connected to each
other. 

Open each of these CSV files and observe how the controllers are
connected to each other. Then try simulating each layout and comparing
the results against each other and the earlier bus layout, using
the FCFS arbiter for all to ensure consistency:

```shell
# Run from the project root, using the same benchmark for both presets.
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

# Run the shared bus with snooping from earlier
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -o tutorial/01-exploration/output/Interconnect/0_Snoop-MSI --trace

# Run a star-based layout on a NoC
./build/Octopus_Simulator -s MultiCoreSystem_Mesh \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "interconnect_type(s)=NoC" \
  -o tutorial/01-exploration/output/Interconnect/1_Star --trace

# Run a mesh-based layout
./build/Octopus_Simulator -s MultiCoreSystem_Mesh \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "interconnect_type(s)=Mesh" \
  -o tutorial/01-exploration/output/Interconnect/2_Mesh --trace
```

Try graphing the trend with the python script `sweeps/plot_axis.py`.
See if it matches your expectations.

## If you finish early

Repeat the arbiter comparison on `cacheb01-trace` — the benchmark where all four
cores fight over one block. Save these in a separate axis folder such as `output/Arbiter-cacheb01/` so the
`a2time01` results remain available. Compare how strongly arbitration affects
each benchmark.

`sweeps/` holds the scripts that sweep these axes in bulk and produce the figures in
the papers; `sweep_protocols.sh` is the readable entry point.
