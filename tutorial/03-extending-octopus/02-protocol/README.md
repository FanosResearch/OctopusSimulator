# 03/02 — Add a coherence protocol with a CSV

**Goal:** read a protocol FSM, compare MI with MSI, and select MI through a system
configuration CSV. No C++ changes or rebuild. About 20 minutes; run from the
project root.

## 1. Read the FSM layout

Protocol tables live in `Protocols_FSM/`. L1 and LLC controllers load separate
files: snooping MSI uses `MSI_splitBus_snooping.csv` at L1 and `MSI_LLC.csv` at LLC.

Open `Protocols_FSM/MSI_splitBus_snooping.csv`. Each file contains:

- `EventNum,Event`: the numbered events recognized by the protocol handler.
- `ActionNum,Action`: the numbered actions the handler can execute.
- `StateNum,State`: stable and transient states with their numeric IDs.
- `State,stable,isDataValid,...`: the transition table, one row per state and
  one column per event. The two flags identify stable states and valid data.

A transition cell contains actions separated by `/`, followed by the next state:

| Cell | Meaning |
|---|---|
| `GetM/IM_ad` | Issue `GetM`, then enter `IM_ad` |
| `Hit/Data2Req/I` | Execute `Hit` and `Data2Req`, then enter `I` |
| `Hit/` | Execute `Hit`, keeping the current state |
| `IM_d` | Change state without an action |
| Empty | No action; keep the current state |
| `Fault/` | Report an unexpected event |

Keep transition rows in **state-ID order** and event columns in **event-ID order**;
lookup uses their positions. Preserve the blank lines separating the numbered
lists. CSV names and IDs must match the handler's supported events and actions.

Find state `I` and event `Load`. What request does MSI issue, and which transient
state does it enter while waiting?

## 2. Compare the supplied MI example

Open this exercise's [MI_splitBus_snooping.csv](MI_splitBus_snooping.csv) beside
`Protocols_FSM/MSI_splitBus_snooping.csv`.

| Behavior | MSI | MI |
|---|---|---|
| Stable states | Modified, Shared, Invalid | Modified, Invalid |
| Load in `I` | `GetS/IS_ad` | `GetM/IM_ad` |
| Load in `M` | `Hit/` | `Hit/` |
| Read sharing | Multiple caches can hold `S` copies | One cache owns the line in `M` |

MI requests exclusive ownership even for a load. It removes the shared state and
its acquisition paths, retaining transient states for outstanding transactions.
A second core reading the same line must obtain ownership from the first.

Find MI's `M` row and `Other_GetM` column: `Data2Req/I` supplies the line to the
requester and invalidates the old copy. Compare MSI's `M` / `Other_GetS` cell.
Predict what happens when two cores repeatedly read the same line.

MI uses MSI's existing event/action vocabulary, so it can reuse the `SNOOP_MSI`
handler and MSI LLC table. Adding new event or action semantics would require C++
changes; editing a table alone does not implement new handler behavior.

## 3. Install the table and select it in the config

The loader resolves `fsm_filename` under `Protocols_FSM/` and appends `.csv`.
Copy the tutorial table there under its own name:

```shell
cp tutorial/03-extending-octopus/02-protocol/MI_splitBus_snooping.csv \
  Protocols_FSM/Tutorial_MI_splitBus_snooping.csv
```

Open [MultiCoreSystem_Snoop_MI.csv](MultiCoreSystem_Snoop_MI.csv) in this exercise.
It is the snoop MSI preset with the L1 table changed:

```csv
cache_controller[*].fsm_filename(s),Tutorial_MI_splitBus_snooping
```

Check the matching settings in that file:

```csv
cache_controller_type(s),CacheController
cache_controller[*].protocol_type(s),SNOOP_MSI
llc_controller_type(s),CacheController_End2End
llc_controller.protocol_type(s),SNOOP_LLC_MSI
llc_controller.fsm_filename(s),MSI_LLC
```

`[*]` selects the MI table for every private L1. `protocol_type` selects the C++
handler; `fsm_filename` selects its transition table. There is no `SNOOP_MI` handler
needed for this example.

## 4. Run MSI and MI on the same workload

The supplied `mi_pingpong` workload has cores 0 and 1 repeatedly read line
`0x1000`; cores 2 and 3 have no memory accesses. Exercise 00 prepares the benchmark
traces; if this workload is missing, run `bash get_benchmarks.sh BMs/eembc-traces`.

```shell
W=$PWD/BMs/eembc-traces/mi_pingpong
# Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/mi_pingpong")

./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Snoop \
  -p "workload_path(s)=$W/" \
  -o tutorial/03-extending-octopus/02-protocol/output/MSI --trace

./build/Octopus_Simulator -s MultiCoreSystem \
  -c ./tutorial/03-extending-octopus/02-protocol/MultiCoreSystem_Snoop_MI.csv \
  -p "workload_path(s)=$W/" \
  -o tutorial/03-extending-octopus/02-protocol/output/MI --trace
```

Both commands use the same C++ system wiring. `-c` selects the system CSV; the
second run selects MI through that CSV, without changing `MultiCoreSystem.csv`.

## 5. Check your prediction

```shell
python3 sweeps/plot_axis.py tutorial/03-extending-octopus/02-protocol/output
./octoviz.sh serve tutorial/03-extending-octopus/02-protocol/output
```

The plotter requires matplotlib and NumPy. Read `Summary_transposed.csv` in each
run directory to compare finish cycle and request/response bus delays, then filter
the coherence transitions to address `0x1000`.

- Does MSI's first load follow `I → IS_ad`, while MI follows `I → IM_ad`?
- Where does MSI allow shared copies? Where does MI transfer ownership?
- Does the extra ownership traffic explain the difference in completion time?

Use the largest finish cycle across cores. Do not expect zero contention simply
because the trace contains only reads. If you rerun an already converted setting,
refresh it with `./octoviz.sh convert <setting-directory>`.
