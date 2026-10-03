# 02 — The configuration model and concurrency limits

**Goal:** understand configuration inheritance and explore how CPU, cache-miss,
and write-back limits constrain concurrency. About 30 minutes. Use the binary
built in exercise 00; run commands from the **project root**.

## 1. Follow one parameter through the hierarchy

Configuration files roughly mirror `src/` and `header/`. For example,
`SplitBusController` has source in `src/Interconnect/`, a header in
`header/Interconnect/`, and defaults in `configuration/Interconnect/`.

Open `Interconnect/SplitBusController.csv`. Its first line is:

```csv
Extends,BusController
```

`Extends` tells the CSV loader to read `BusController.csv` from the same directory
**first**, then apply the remaining rows of this file. It must be the first line.
This is explicit CSV inheritance; C++ inheritance alone does not load defaults.
Here, `BusController.csv` sets `arbiter_type(s),RRArbiter`, while
`SplitBusController.csv` overrides it with `arbiter_type(s),TDMArbiter`.
The shared bus uses `TripleBusController.csv`, which extends
`SplitBusController.csv` and inherits that TDM default.

The system constructs each bus, and each bus constructs its controller. Each
component loads its own CSV defaults, then merges the parameter map passed by its
owner, replacing matching keys. The shipped system selects FCFS for the shared
bus, giving this three-level example:

| Level | File under `configuration/` | Row |
|---|---|---|
| Base controller | `Interconnect/BusController.csv` | `arbiter_type(s),RRArbiter` |
| Split controller | `Interconnect/SplitBusController.csv` | `arbiter_type(s),TDMArbiter` |
| System | `SystemConfigurations/MultiCoreSystem.csv` | `bus[0].interconnect_controller.arbiter_type(s),FCFSArbiter` |

The controller CSV uses the local key `arbiter_type`. In the system CSV,
`bus[0].interconnect_controller` routes the setting through the bus to its
controller. Dotted names identify child objects, not folders. The C++
constructors pass those parameter maps explicitly.

### Compare the inherited and system settings

Run from the project root, starting with the committed CSVs. First save the
shipped configuration:

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Arbiter/0_System-FCFS --trace --PrintConfig
```

Use no `-c` here: these runs read `MultiCoreSystem.csv`. `--PrintConfig` saves
resolved settings as `config.log` in each run's output directory.

Now temporarily remove this row from `configuration/SystemConfigurations/MultiCoreSystem.csv`:

```csv
bus[0].interconnect_controller.arbiter_type(s),FCFSArbiter
```

Without that system override, the shared bus inherits TDM from
`SplitBusController.csv`. The memory bus uses `Point2PointController.csv`, which
extends `BusController.csv` directly, so it inherits RR.

```shell
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Arbiter/1_Inherited-TDM --trace --PrintConfig
```

Restore the FCFS row in `MultiCoreSystem.csv` before continuing.
Check each run's saved `arbiter_type` settings: the `TripleBusController` entry
is the shared bus, and `Point2PointController` is the memory bus.

### Target all buses or one bus

Parameter names are case-sensitive: use lowercase `bus`.

- `bus[*]` supplies a value to every bus in the array.
- `bus[0]` targets the L1–LLC shared bus.
- `bus[1]` targets the LLC–DRAM bus.

An instance-specific key takes precedence over a matching wildcard key. For
example, `bus[*].interconnect_controller.arbiter_type(s),RRArbiter` would select
RR for the memory bus while the existing `bus[0]` FCFS row would still select
FCFS for the shared bus.

## 2. Apply a command-line override

The suffix gives the value's type: `(i)` integer, `(s)` string, `(vi)` vector of
integers, `(vs)` vector of strings. CSV rows use a comma; `-p` uses `=`.

With the system FCFS row restored, override just the shared bus for one run:

```shell
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter" \
  -o tutorial/02-configuration/output/Arbiter/2_CLI-RR --trace --PrintConfig
```

Command-line overrides take precedence over the selected system CSV. Quote them
to preserve brackets, parentheses, and spaces. The CSV still selects FCFS;
rerunning the first command uses FCFS again. The memory bus remains RR in all
three runs.

Compare the saved settings, summaries, and timelines:

```shell
grep 'arbiter_type' tutorial/02-configuration/output/Arbiter/*/config.log
python3 sweeps/plot_axis.py tutorial/02-configuration/output/Arbiter
./octoviz.sh serve tutorial/02-configuration/output/Arbiter
```

Compare finish cycle and worst-case request/response bus latency across cores.
These measured delays include waiting, so changing arbitration can affect both
the selected bus and contention elsewhere. Which setting comes from CSV
inheritance, which comes from the system, and which comes from the command line?

### Restore before the next experiment

Ensure the system FCFS row is restored before the capacity experiments.
To restore the file to the current commit with Git, use the command below.
This discards all uncommitted edits to that CSV; saved run outputs remain available.

```shell
git restore -- configuration/SystemConfigurations/MultiCoreSystem.csv
```

## 3. CPU, MSHR, and write-back limits

Exercise [01](../01-exploration/README.md#switching-coherence-families-with-a-system-preset)
covers system classes and presets. For the experiments below, keep
`MultiCoreSystem_Snoop` and FCFS arbitration fixed.

These capacities constrain different parts of a request's lifetime:

| Resource | Parameter | Snoop preset baseline | What occupies a slot |
|---|---|---|---|
| CPU outstanding requests | `cpu[*].m_number_of_OoO_requests(i)` | 8 per core, inherited from `CPU.csv` | An issued memory request until its response returns |
| L1 MSHRs (miss status holding registers) | `cache_controller[*].num_mshr(i)` | 16 per L1 | An outstanding miss to a cache block |
| LLC MSHRs | `llc_controller.num_mshr(i)` | 64 shared | An outstanding miss at the LLC |
| L1 write-back buffer (WB/PWB) | `cache_controller[*].m_data_handler.pwb_size(i)` | 8 per L1 | An evicted cache line awaiting write-back handling |
| LLC write-back buffer | `llc_controller.m_data_handler.pwb_size(i)` | 32 shared | An evicted LLC line awaiting write-back handling |
| L1 / LLC processing queues | `cache_controller[*].processing_queue_size(i)` / `llc_controller.processing_queue_size(i)` | 16 / 64 | Queued controller work; demand admission is bounded |

The CPU's “OoO” setting is a limit on outstanding **memory requests**, not a full
instruction reorder-buffer model. At 1, a core waits for each response before
issuing another request. At 8, it can overlap up to eight requests when the trace's
compute gaps and downstream resources permit. Use positive integers for this
setting; `-1` is not an unbounded CPU mode.

MSHRs track misses rather than every CPU request. Hits and accesses to an already
tracked block do not require another miss slot. A full MSHR set stalls admission
of a new miss until a slot becomes available. Raising the L1 limit above the CPU's
outstanding-request limit may make little difference; raising the CPU limit may
instead expose an L1 or shared LLC bottleneck.

Write-back buffers preserve evicted lines while their write-backs are handled,
allowing cache replacement to overlap other work. When a replacement needs a
buffer slot and none is available, the fill must wait. This matters most for
workloads with frequent replacements; a workload with few evictions may show no
change. The WB buffer is separate from the CPU request limit and the MSHRs.

MSHR, WB, and processing-queue limits accept `-1` for unbounded capacity. Use finite
positive values in these comparisons. Increasing capacity can improve overlap
while increasing contention, so finish time and worst-case request latency need
not improve together.

## 4. Stretch: Compare one limit at a time

First save the same baseline under each axis so the plotting layout matches
exercise 01, before modifying the value of each parameter
and comparing with the baseline:

```shell
for axis in CPU MSHR WB; do
  ./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
    -p "workload_path(s)=$W/" \
    -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
    -o "tutorial/02-configuration/output/$axis/Baseline" --trace
done

# CPU: one outstanding memory request per core instead of eight.
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cpu[*].m_number_of_OoO_requests(i)=1" \
  -o tutorial/02-configuration/output/CPU/One --trace

# MSHR: one outstanding miss slot per L1 instead of sixteen.
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cache_controller[*].num_mshr(i)=1" \
  -o tutorial/02-configuration/output/MSHR/One --trace

# WB: one write-back slot per L1 instead of eight; keep the LLC unchanged.
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cache_controller[*].m_data_handler.pwb_size(i)=1" \
  -o tutorial/02-configuration/output/WB/One --trace
```

Compare each setting's `Summary_transposed.csv` and timeline, or plot each axis:

```shell
for axis in CPU MSHR WB; do
  python3 sweeps/plot_axis.py "tutorial/02-configuration/output/$axis"
done
./octoviz.sh serve tutorial/02-configuration/output
```

Record finish cycle, mean effective
latency, worst-case total latency, CPU delay, and L1 stall for each setting. Use
maxima across all cores for finish and worst cases. Stage maxima are independent;
do not add them to obtain worst-case total latency.

- Does reducing the CPU limit move delay into the CPU stage? How much overlap
  disappears from the timeline?
- Does reducing L1 MSHRs increase L1 stall? Does it change contention downstream?
- Does reducing the WB buffer change this workload? Inspect replacements and
  write-back traffic before attributing an unchanged result to an unused limit.

If you rerun an already converted setting, refresh it with
`./octoviz.sh convert <setting-directory>` before viewing it again.

For a further experiment, add `CPU/Sixteen` with the CPU limit set to 16, or vary
`llc_controller.num_mshr(i)` or `llc_controller.m_data_handler.pwb_size(i)` under a
new axis with its own baseline. Keep the workload and other settings fixed. Which
resource becomes the next bottleneck?
