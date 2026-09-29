<p align="center">
  <img src="docs/imgs/logo.png" alt="Octopus" width="170">
</p>

# Octopus
Octopus is a cycle-accurate cache system simulator with flexible interconnect models. It simulates various cache system and interconnect components, including controllers, data arrays, coherence protocols, and arbiters. Octopus enables the user to build reconfigurable simulation infrastructure for multicore processor chip with a high degree of flexibility of controlling system's configuration parameters. Octopus is implemented in C++ using object-oriented programming concepts to support a modular, expansible, configurable, and integrable design.

### The problem — why the field needs a common framework
![What we lose without a common, cycle-accurate simulation framework](docs/imgs/problems_we_address.png)

### The solution — Octopus
![What we gain: a common, cycle-accurate simulation framework](docs/imgs/solution.png)

# Documentation
* **[Supported configurations & features](SUPPORTED_CONFIGURATIONS.md)** — the full capability matrix: coherence protocols, interconnect topologies, bus arbitration, cache hierarchy, memory systems (incl. MCsim), operating/integration modes, predictable caching, and monitoring.
* **[Architecture deep-dive](docs/Architecture.md)** — how the clocked, configurable components fit together, the CSV-FSM coherence engine, a request's end-to-end journey, and how to extend the tool.
* **[Adding a coherence protocol](docs/AddingAProtocol.md)** — a worked example: implement **MI** (Modified/Invalid) as a CSV finite-state machine with no C++ and no recompile, with the MI-vs-MESI coherence trace.
* **Monitoring internals:** [Logger](docs/Logger.md) (per-request, per-stage latency + worst-case) · [Debugger](docs/Debugger.md) (filterable component/coherence tracing).
* **[Reproducing the results](REPRODUCIBILITY.md)** — from a fresh clone to every figure and table via configuration-only changes; includes the paper artifact map. See also [`sweeps/README.md`](sweeps/README.md).
* **[Results & figures](results/)** — pre-generated sweep CSVs and the paper figures; [`results/README.md`](results/README.md) explains the layout, columns, and figure-to-paper mapping.
* **[Publications built on Octopus](PUBLICATIONS.md)** — peer-reviewed works that were evaluated on Octopus or re-implemented in it (continuously updated).

# Architecture

Octopus is built from modular, **clocked** components that are *configured, not
hard-coded*, and connected through bounded, back-pressured interfaces. The
diagrams below (from the Octopus CAL paper) explain the design; the full
capability matrix is in [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md).

### System organization

![Octopus system organization](docs/imgs/organization.png)

A configurable multi-core system: clusters of CPU cores, each with private caches
joined by a direct interconnect, connected through configurable interconnects to a
shared last-level cache (LLC) and main memory. Hierarchy depth, private/shared
placement, topology, and per-level parameters are all set by configuration.

### Class design (UML)

![Octopus UML class diagram](docs/imgs/uml.png)

Every simulation entity derives from `Configurable` (hierarchical parameters) and
`ClockedObj` (cycle-stepped by the `ClockManager`). Coherence is a
`CoherenceProtocolHandler` driven by an `FSMReader`; an interconnect is an
`InterconnectTopology` + `InterconnectController` + `Arbiter`; a cache splits into a
`CacheController` and a `CacheDataHandler`. New protocols, arbiters, topologies, and
replacement policies are added by subclassing — or, for coherence, by editing a CSV.

### Inside a cache controller

![Cache controller internals](docs/imgs/cache_controller.png)

Messages enter the processing queue; the **protocol handler** interprets the CSV FSM
through the **FSM reader** and emits controller actions; the **data handler** serves
the arrays via a replacement policy, MSHRs, a write-back buffer, and a port arbiter.

### Interconnect

![Interconnect internals](docs/imgs/interconnect.png)

An interconnect is an `InterconnectTopology` (message-holding *interfaces* plus a
*connection map*) and an `InterconnectController` whose *arbiter* resolves contention
on shared links. Swapping the topology/controller yields point-to-point, unified bus,
split bus, mesh, or NoC.

### Coherence as an editable CSV finite-state machine

![Coherence FSM CSV example](docs/imgs/fsm_example.png)

Each coherence protocol is a finite-state machine stored as a **CSV table**: rows are
(stable and transient) states, columns are coherence events, and each cell is the
action(s) and next state. Adding or modifying a protocol is a spreadsheet edit — no
C++ change and no recompilation.

### Hierarchical configuration

![Configuration propagation](docs/imgs/configuration.png)

Configuration propagates from the CLI through the top-level module down to every
sub-component and FSM; parameters are inherited, overridden, and extended. This is
what lets a single build sweep the entire design space
(see [`REPRODUCIBILITY.md`](REPRODUCIBILITY.md)).

> **Deeper dive:** [`docs/Architecture.md`](docs/Architecture.md) walks through the clock model, the coherence engine, a request's end-to-end journey, the integration modes, and how to add new protocols and components.

# Citation
If you use this simulator in your work, please consider cite:


Hossam, Mohamed, Salah Hessien, and Mohamed Hassan. "Octopus: a Cycle-Accurate Cache System Simulator." IEEE Computer Architecture Letters (2024). [Octopus](https://ieeexplore.ieee.org/iel8/10208/10700665/10633788.pdf?casa_token=2ABvIsydo2gAAAAA:hsgmaeaOe9CCwKII0mMr86OjOAPGSbHmI-9uq2-vg0GLbnT9YLhiS-nN1RYYT4d8jV2cmhsJQrs).

# Getting started
* The simulator is tested on both Linux Ubuntu 18.04.4 LTS and Ubuntu 20.04.01 releases. You may consider using Virtual Machine VM to install Ubuntu on your machine if it is not your primary operating system.  
* `$Octopus` refers to the top level directory where Octopus resides.
* Directory `$Octopus/src/` contains the source code of the simulator.
* Directory `$Octopus/header/` contains the header files.
* Directory `$Octopus/Protocols_FSM/` contains the CSV files that defines the coherency protocols' finite state machines.
* Directory `$Octopus/configuration/` contains the CSV files that contains the configuratable parameters of the simulation components.

## Zero-install: run Octopus in a browser (GitHub Codespaces)

The `esweek-tutorial` branch carries a dev container, so you can get a working
simulator without installing anything locally. On GitHub: **Code → Codespaces →
Create codespace on esweek-tutorial**. The container installs the toolchain and the
Python packages, fetches the EEMBC traces, builds the simulator and runs the
environment check; port 8765 is forwarded so the visualizer opens by itself.

Everything lives in two files you can also read as documentation of the
dependencies: `.devcontainer/on-create.sh` (packages and benchmark traces) and
`.devcontainer/post-create.sh` (build and check).

## Checking an environment

On any machine — a codespace, a laptop, a cluster node — one command says whether
the environment is usable:

```shell
bash scripts/check_environment.sh
```

It prints one line per component and finishes with a short simulation whose result
is fixed: **15364 requests, worst-case DRAM latency 359 cycles**. The simulator is
deterministic and platform-independent, so those numbers are an equality check, not
a smoke test — a mismatch means a stale build, an edited configuration, or a bug.
It also catches the one portability trap in the tree: a working copy checked out on
Windows and then copied to Linux carries CRLF line endings, and MCsim's `.ini`
parser rejects them with `Malformed Line N (missing equals)`.

## Fetching only part of the benchmark suite

`get_benchmarks.sh` clones the whole benchmark repository (~710 MB). The SPLASH
archives are ~651 MB of that, so an environment that only runs EEMBC can ask for
a sparse clone — about 61 MB, and a few seconds instead of a few minutes:

```shell
BMS_SPARSE="eembc-traces" ./get_benchmarks.sh
```

This affects only a first clone. To add the rest later, remove `BMs/` and run
`./get_benchmarks.sh` again.

## Building Octopus
Octopus uses CMake to manage the build system of the simulator. In order to build Octopus, you need to install the following:

```shell
sudo apt update
sudo apt upgrade
sudo apt-get install build-essential cmake
```

In order to build the simulator, we create a directory `$Octopus/build/`

```shell
mkdir $Octopus/build/
cd $Octopus/build/
cmake ../ .
make
```

Building for debug will require an extra flag to CMake

```shell
cd $Octopus/build/
cmake ../ . --DCMAKE_BUILD_TYPE=Debug
make
```

## Running Octopus

Running the simulator requires to choose a system configuration to run and a workload. In the example, we choose to run MultiCoreSystem with the workload TestBM in `$Octopus/BMs/TestBM/`. `$Octopus` should be replaced with the full path of the simulator's directory.

```shell
cd $Octopus/build/
./Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$Octopus/BMs/TestBM/"
```
`-s` is used to specify the configuration, and `-p` is to overwrite any parameter in the configuration.

The default output reports will be found in `$Octopus/BMs/TestBM/newLogger/`.

> **The benchmarks are a separate repository**
> ([`FanosResearch/OctopusBMs`](https://github.com/FanosResearch/OctopusBMs)),
> cloned into `BMs/` on first use; its SPLASH-2 traces are stored compressed
> (~10 GB inflated). If you point the binary at a benchmark directory yourself,
> run `./get_benchmarks.sh` first (idempotent: clones if absent, then inflates).
> The driver scripts (`run_octopus.sh`, `run_splash.sh`, `sweep_protocols.sh`,
> `sweeps/*`) do this automatically — see
> [`REPRODUCIBILITY.md`](REPRODUCIBILITY.md).

A worked end-to-end example of the simulator driving something visible is
[`demo/`](demo/README.md): a periodic localization task ([`docs/Tasks.md`](docs/Tasks.md)) whose
job timings steer a robot along a planned path, showing what shared-cache and DRAM interference
cost a real-time task, and what a reserved cache way recovers.

When running the binary directly, use `-o <directory>` to write the logger CSVs
(`LatencyReport_C*.csv`, `Summary.csv`, and task `JobReport_C*.csv`) to a separate
directory. Missing directories are created; relative paths resolve from the current
working directory. Without `-o`, reports still go to `<workload_path>/newLogger`.
Reusing an output directory overwrites reports with matching names. For example:

```bash
./build/Octopus_Simulator -s MultiCoreSystem -o results/rr \
  -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter"
```

This also works with `-s MultiCoreSystem_Mesh`. Add `--trace` to record raw events
as `trace.bin` (plus `trace.bin.names`) in the same output directory:

```bash
# From tutorial/, using the workload configured in the system CSV:
../build/Octopus_Simulator -s MultiCoreSystem -o Arbiter/FCFS --trace \
  -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter"
```

Relative `workload_path` values from system CSVs resolve against the project root
(derived from the compiled-in configuration directory). Explicit
`-p "workload_path(s)=..."` overrides and `-o` paths resolve against the current
working directory; absolute paths are unchanged. Without `-o`, `--trace` writes
into `<workload_path>/newLogger/`. Without `--trace`, tracing stays off unless
`OCTOPUS_TRACE` is set. That environment variable remains supported and takes
precedence as an explicit trace filename, including when `--trace` is supplied.
`OCTOPUS_TRACE_WINDOW` still controls the recorded cycle range.

To *see* a run rather than read its reports, `./octoviz.sh view <workload_dir>` simulates it with
the raw event trace on, converts the run and opens the timeline viewer in the browser (per-request
pipeline, resource lanes for every message, a "why did I wait" view and a per-line coherence
transition table); `./octoviz.sh serve <dir>` serves runs converted earlier. Needs
`pip install duckdb numpy`. See [`docs/Visualizer.md`](docs/Visualizer.md) and
[`docs/Trace.md`](docs/Trace.md).
