# 00 — Setup and first run

**Goal:** a working environment, one completed simulation, and a first read of its
output. About 15 minutes.

## 1. Is the environment healthy?

```shell
bash scripts/check_environment.sh
```

Eight `[PASS]` lines ending with:

```
[PASS] sample run     15364 requests, worst DRAM 359 cycles
ALL CHECKS PASSED
```

Those two numbers are exact. Anyone in the room whose numbers differ has a real
difference, not noise — say so, and we'll look. (The commonest cause is an edited file
under `configuration/`; `git checkout -- configuration/` puts everything back.)

If you are not in a codespace and the simulator is not built yet:

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
```

## 2. Run one benchmark

`a2time01` from EEMBC, four cores, on the configuration as shipped: snooping MESI,
a TDM-arbitrated split bus, fixed-latency memory.

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/"
```

(On Windows in Git Bash, use `W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")`.)

It takes a few seconds. The `-s` argument names the system configuration —
`configuration/SystemConfigurations/MultiCoreSystem.csv` — and `-p` overrides any
parameter in it from the command line. You will use `-p` a lot.

## 3. Read the output

Everything lands in the workload's `newLogger/` directory:

```shell
column -s, -t $W/newLogger/Summary.csv | less -S       # one row per core, worst cases
head -3 $W/newLogger/LatencyReport_C0.csv | column -s, -t   # one row per request
```

`Summary.csv` gives, per core, the worst-case latency of every stage and the finish
cycle. `LatencyReport_C<n>.csv` has one row per memory request with its latency split
into stages: CPU, L1 stall, request bus, L2 stall, L2 access, response bus, L2–DRAM
bus, DRAM, L1 access. A total tells you a core was slow; the stages tell you **which
shared resource** made it slow. That decomposition is the reason the tool exists.

## 4. Check

```shell
bash tutorial/00-setup-and-first-run/check.sh
```

It re-runs the benchmark and compares `Summary.csv` against `expected/Summary.csv`
byte for byte.

## 5. The visualizer

```shell
./octoviz.sh view BMs/eembc-traces/a2time01-trace
```

About 8 seconds: it re-runs the benchmark with the raw event trace on, converts it,
and serves it. In a codespace it prints the URL to ctrl-click (or use the **Ports**
panel, port 8765, globe icon); on a laptop it opens your browser.

Three views to find, in this order: the **request pipeline** (one row per request,
stages as spans), the **resource lanes** below it (what the buses, the LLC array and
DRAM were doing in those same cycles), and the **coherence transition table** (filter
by an address; every FSM transition on that line, with each agent's state). They are
the same events seen from the request's side, the resource's side and the protocol's
side.

## The trap

`./run_octopus.sh --protocol <snoop|directory>` — which you will meet in the next
exercise — *copies* a preset over `MultiCoreSystem.csv`. Any hand edit to that file
is overwritten. Keep edits in `-p` overrides, or expect to `git checkout --
configuration/`.
