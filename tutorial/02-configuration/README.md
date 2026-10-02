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
Here, request/response latencies come from `BusController.csv`, while
`SplitBusController.csv` replaces the arbiter default with TDM.

The system constructs each bus, and each bus constructs its controller. Each
component loads its own CSV defaults, then merges the parameter map passed by its
owner, replacing matching keys. Dots route parameters to children:

|File|Key to override|
|:--|:--|
| `MultiCoreSystem.csv` | `bus[0].interconnect_controller.m_request_latency` |
| `Bus.csv` | `interconnect_controller.m_request_latency`|
| `Controller.csv` | `m_request_latency` |

The C++ constructors pass those maps explicitly; the dotted names do not select
folders. The controller constructor supplies the path to its default CSV.
The shared bus is a `TripleBus`; its `TripleBusController.csv` extends
`SplitBusController.csv`, so the same latency defaults apply.

### Run the starting configuration

Run from the project root in your Codespace, starting with the committed CSVs.
This exercise edits four files; restore them with Git at the end.

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")

./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/Baseline --trace --PrintConfig
```

Use no `-c` here: these runs read `MultiCoreSystem.csv`. Each run saves reports and
a trace under `output/Latency/<setting>/`. Reusing a setting replaces its reports.
Every latency run uses `--PrintConfig` to save its resolved settings as `config.log`
in that run's output directory.

### Override the same latency at each level

Keep earlier edits in place. Edit an existing row if present; otherwise append it.
Do not change response latency or clock periods.

| Step | File under `configuration/` | Row to set | Output setting |
|---|---|---|---|
| 1 | `Interconnect/BusController.csv` | `m_request_latency(i),4` | `Parent` |
| 2 | `Interconnect/SplitBusController.csv` | `m_request_latency(i),8` | `Controller` |
| 3 | `Interconnect/Bus.csv` | `interconnect_controller.m_request_latency(i),16` | `Bus` |
| 4 | `SystemConfigurations/MultiCoreSystem.csv` | `bus[*].interconnect_controller.m_request_latency(i),24` | `All` |

Run each command immediately after its matching edit, before making the next edit:

```shell
# After step 1: parent controller defaults.
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/Parent --trace --PrintConfig

# After step 2: split-bus controller override.
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/Controller --trace --PrintConfig

# After step 3: bus-level override.
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/Bus --trace --PrintConfig

# After step 4: system override for every bus.
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/All --trace --PrintConfig
```

The configured `(bus[0], bus[1])` request latencies become `(4,4)`, `(8,4)`, `(16,16)`,
and `(24,24)`. Step 2 targets only the shared bus: the memory bus uses a
`Point2PointController`, which extends `BusController`, bypassing `SplitBusController`.

After each run, check the saved request-latency settings:

```shell
rg 'm_request_latency' tutorial/02-configuration/output/Latency/*/config.log
```

The final `TripleBusController` entry describes `bus[0]`; `Point2PointController`
describes `bus[1]`. The temporary `SplitBusController` entry printed during
TripleBus construction has the same settings.

Read each run's `Summary_transposed.csv`. Compare **Worst-case Requst Bus Latency**
(the L1–LLC request bus; the label retains its original spelling) and
**Worst-case L2-DRAM Bus Latency** (the LLC–DRAM bus). Take the maximum across cores.
Which delays increase, stay similar, or decrease relative to the preceding run?

The configured values are service times in interconnect cycles; summary values
also include waiting. A change on one bus can affect contention on the other, so
an unchanged setting does not guarantee an unchanged worst-case delay.

### Target all buses or one bus

Parameter names are case-sensitive: use lowercase `bus`.

- `bus[*]` supplies a value to every bus in the array.
- `bus[0]` targets the L1–LLC shared bus.
- `bus[1]` targets the LLC–DRAM bus.

Keep the wildcard row from step 4 and add these two rows to `MultiCoreSystem.csv`:

```csv
bus[0].interconnect_controller.m_request_latency(i),32
bus[1].interconnect_controller.m_request_latency(i),48
```

```shell
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/02-configuration/output/Latency/PerBus --trace --PrintConfig
```

The configured request latencies are now `(32,48)`. Compare both bus-delay rows
with the `All` run. An instance-specific key takes precedence over the matching
wildcard key, regardless of their order in the file. What would happen if you
removed just the `bus[1]` row?

## 2. Apply a command-line override

The suffix gives the value's type: `(i)` integer, `(s)` string, `(vi)` vector of
integers, `(vs)` vector of strings. CSV rows use a comma; `-p` uses `=`.

```shell
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -p "bus[0].interconnect_controller.m_request_latency(i)=64" \
  -o tutorial/02-configuration/output/Latency/CLI --trace --PrintConfig
```

The configured request latencies are now `(64,48)`. Command-line overrides take
precedence over the selected system CSV. Quote them to preserve brackets,
parentheses, and spaces. Compare the `CLI` and `PerBus` summaries: how does changing
only the shared bus affect both measured delays? Repeating the `PerBus` command
uses `(32,48)` again; the command-line change applies only to its invocation.

Plot the saved runs to compare both bus-delay rows and finish cycle:

```shell
python3 sweeps/plot_axis.py tutorial/02-configuration/output/Latency
./octoviz.sh serve tutorial/02-configuration/output/Latency
```

The plotter requires matplotlib and NumPy. Which edits changed both buses, and
which changed only one? Use the timeline to distinguish service time from waiting.

### Restore before the next experiment

Restore the four CSVs to the current commit before the preset and capacity
experiments. This discards uncommitted edits to these files; saved run outputs
remain available.

```shell
git restore -- configuration/Interconnect/BusController.csv \
  configuration/Interconnect/SplitBusController.csv \
  configuration/Interconnect/Bus.csv \
  configuration/SystemConfigurations/MultiCoreSystem.csv
```

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

Use `-c MultiCoreSystem_Snoop` for snooping MSI. Preset selection itself does not
copy or edit configuration files. `-p` applies after the selected preset.
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

## 6. Stretch: Compare one limit at a time

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
