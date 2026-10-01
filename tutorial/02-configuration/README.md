# 02 — The configuration model and concurrency limits

**Goal:** understand configuration inheritance and explore how CPU, cache-miss,
and write-back limits constrain concurrency. About 20 minutes. Use the binary
built in exercise 00; run commands from the **project root**.

## 1. Hierarchy and inheritance

Open these, in order:

```
configuration/CacheControllers/BaseController.csv          defaults every controller shares
configuration/CacheControllers/CacheController.csv         "Extends,BaseController" + overrides
configuration/CacheControllers/CacheControllerExclusive.csv
configuration/CacheDataHandler_COTS.csv                    write-back buffer defaults
configuration/CPU.csv                                     CPU outstanding-request limit
configuration/SystemConfigurations/MultiCoreSystem_Snoop.csv  instances + per-instance overrides
```

A system CSV overrides per instance: `cache_controller[*].num_mshr(i),16` applies to
every L1, `cache_controller[2].num_mshr(i),4` to one. Interfaces work the same way —
`configuration/Interconnect/SplitBusController.csv` extends `BusController.csv`, and
`bus[0].interconnect_controller.arbiter_type(s)` in the system file overrides it.
A value omitted from the system CSV can still come from a component's defaults.

## 2. Typed keys and command-line overrides

The suffix is the value's type: `(i)` int, `(s)` string, `(vi)` vector of ints,
`(vs)` vector of strings. `-p` overrides a parameter after the CSV is loaded:

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cache_controller[*].num_mshr(i)=1" \
  -o tutorial/02-configuration/output/MSHR/One --trace
```

Quote overrides so the shell preserves `[*]`, parentheses, and paths containing
spaces. Each invocation loads its baseline again: overrides affect only that run.
`-o` saves reports in the named directory; `--trace` saves the raw trace for Octoviz.
Reusing an output directory replaces that run's reports. `output/` is Git-ignored.

## 3. What is data and what is code

`-s` selects the C++ system class, which decides **what objects exist and how they
are wired**. `MultiCoreSystem` creates one L1 per core on `bus[0]`, an LLC between
`bus[0]` and `bus[1]`, and memory beyond. Protocols, arbitration, cache geometry,
and resource limits are parameters read from CSV files.

`-c` (or `--config`) selects the system CSV independently of the class. Without
`-c`, the CSV has the same name as the class. With `-s MultiCoreSystem -c MyConfig`,
`MyConfig.csv` supplies parameters for the existing `MultiCoreSystem` wiring; it
does not require a new C++ class. The `.csv` suffix is optional, and a path such as
`--config ./my-system.csv` also works.

Adding a third cache level changes the wiring and requires C++ work. That is the
boundary crossed in `03-extending-octopus/03-three-levels`.

## 4. Select a preset directly

The snooping and directory presets group compatible controller, protocol, and FSM
settings. Run a directory configuration directly:

```shell
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Directory \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Presets/Directory-MSI --trace
```

Use `-c MultiCoreSystem_Snoop` for snooping MSI. No configuration files are copied
or edited, so no reset is needed afterward. `-p` applies after the selected preset.
The presets also differ in resource sizing; changing presets changes a complete
configuration. For the experiments below, keep the snoop preset and FCFS fixed.

Protocol selection requires matched L1/LLC protocol names, FSM filenames, and
controller types. For example, snooping MESI needs `CacheControllerExclusive` at
L1. See exercise 01 for the coordinated overrides; changing a single protocol
name can leave an incompatible configuration.

## 5. CPU, MSHR, and write-back limits

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

## 6. Compare one limit at a time

First save the same baseline under each axis so the plotting layout matches
exercise 01:

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

# MSHR: section 2 already saved MSHR/One with one slot per L1.

# WB: one write-back slot per L1 instead of eight; keep the LLC unchanged.
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter" \
  -p "cache_controller[*].m_data_handler.pwb_size(i)=1" \
  -o tutorial/02-configuration/output/WB/One --trace
```

Compare each axis's saved summaries and timelines:

```shell
for axis in CPU MSHR WB; do
  python3 sweeps/plot_axis.py "tutorial/02-configuration/output/$axis"
done
./octoviz.sh serve tutorial/02-configuration/output
```

The plotter requires matplotlib and NumPy. Record finish cycle, mean effective
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
