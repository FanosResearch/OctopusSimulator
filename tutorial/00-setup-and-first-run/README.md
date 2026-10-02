# 00 — Setup and first run

**Goal:** a working environment, one completed simulation, and a first read of its
output. About 15 minutes.

Run the commands below from the **project root**. The reference result assumes
the shipped configuration; preserve any personal configuration edits before
restoring that baseline.

## 1. Is the environment healthy?

```shell
bash scripts/check_environment.sh
```

Eight `[PASS]` lines ending with:

```
[PASS] sample run     15364 requests, worst DRAM 377 cycles
ALL CHECKS PASSED
```

Those two numbers are exact. If the environment is set up correctly, the final check should not fail. (The commonest cause is an edited file
under `configuration/`; `git checkout -- configuration/` puts everything back.)

If you are not in a codespace and the simulator is not built yet:

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
```

## 2. Run one benchmark

`a2time01` from EEMBC, four cores, on the configuration as shipped: snooping MESI,
an FCFS-arbitrated split bus, fixed-latency memory.

For convenience, we set a variable to automatically choose the benchmark:
```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
```

Then run the benchmark with the simulator:
```shell
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=$W/" \
  -o tutorial/00-setup-and-first-run/output --trace
```

(On Windows in Git Bash, use `W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")`.)

It takes a few seconds. The `-s` argument names the system configuration —
`configuration/SystemConfigurations/MultiCoreSystem.csv` — and `-p` overrides any
parameter in it from the command line. You will use `-p` a lot.

`-o` creates the output directory and puts the CSV reports there. `--trace` also
records `trace.bin` and its names sidecar in that directory for the visualizer.
Repeating this command overwrites that run. Explicit `-p workload_path` and `-o`
paths are relative to your current directory unless absolute; a workload path
read from the system CSV is relative to the project root.

Note that if you do not specify `-o`, the data will instead be saved
to a subdirectory `newLogger` within the corresponding 
benchmark folder (in `BMs/`).

## 3. Read the output

The saved run is in this exercise’s Git-ignored `output/` directory; `expected/`
remains the reference:

```shell
column -s, -t tutorial/00-setup-and-first-run/output/Summary_transposed.csv | less -S   # metrics as rows, cores as columns
head -3 tutorial/00-setup-and-first-run/output/LatencyReport_C0.csv | column -s, -t   # one row per request
```

`Summary_transposed.csv` shows one metric per row and one column per core,
including worst-case stage latencies and finish cycle. `Summary.csv` contains the
same values in the original layout for scripts and checks. `LatencyReport_C<n>.csv` has one row per memory request with its latency split
into stages: CPU, L1 stall, request bus, L2 stall, L2 access, response bus, L2–DRAM
bus, DRAM, L1 access. A total tells you a core was slow; the stages tell you **which
shared resource** made it slow. That decomposition is the reason the tool exists.

Where each column is spent — on which component of the system a request is waiting,
moving or being served — is drawn on the system itself in
[`docs/imgs/system_end_to_end.svg`](../../docs/imgs/system_end_to_end.svg): one LLC miss
from core 0, routed through the cores, the bus, the LLC, the memory bus and DRAM, each
hop coloured by its column, and its hit counterpart in
[`docs/imgs/system_end_to_end_hit.svg`](../../docs/imgs/system_end_to_end_hit.svg),
where the route stops at the LLC's array port. Which two events in the simulator bound each column — for
an LLC hit and for an LLC miss — is the same route on a time axis, in
[`docs/imgs/request_end_to_end.svg`](../../docs/imgs/request_end_to_end.svg)
(both explained under "A request's journey" in [`docs/Architecture.md`](../../docs/Architecture.md)).
Keep them open while you read your first report.

## 4. Check

```shell
bash tutorial/00-setup-and-first-run/check.sh
```

This checks your saved `output/Summary.csv` against `expected/Summary.csv`,
ignoring Windows CRLF differences. It does not rerun the simulator or modify any
files. If the saved output is missing, run step 2 first. On a mismatch, inspect
the printed differences and check that your run used the shipped baseline.
The check validates the summary, not raw tracing.

The preflight tool in step 1, `scripts/check_environment.sh`, is separate: it
checks the environment and runs its own sample simulation.

## 5. The visualizer

```shell
./octoviz.sh serve tutorial/00-setup-and-first-run/output
```

This discovers the saved reports, converts them automatically if Parquet files
are missing, and displays the run without rerunning the simulator. Automatic
conversion preserves `trace.bin`. Existing conversions are reused; if you rerun
the simulator into the same output directory, explicitly refresh them with
`./octoviz.sh convert tutorial/00-setup-and-first-run/output` (add `KEEP_TRACE=1`
to retain the raw trace during that explicit conversion).

In a codespace it prints the URL to ctrl-click (or use the **Ports** panel, port
8765, globe icon); on a laptop it opens your browser.

Three views to find, in this order: the **request pipeline** (one row per request,
stages as spans), the **resource lanes** below it (what the buses, the LLC array and
DRAM were doing in those same cycles), and the **coherence transition table** (filter
by an address; every FSM transition on that line, with each agent's state). They are
the same events seen from the request's side, the resource's side and the protocol's
side.

