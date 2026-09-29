# 03/02 — Change a coherence protocol

**Goal:** edit a protocol's finite-state machine and see the effect. No compiler
involved: the FSM is a CSV read at startup. About 30 minutes.

## Where the protocol lives

`Protocols_FSM/*.csv`. One table per agent: `MESI_splitBus_snooping.csv` is the L1,
`MESI_LLC.csv` the LLC. Each file has three lists (events, actions, states) and then
the transition table — one row per state, one column per event, each cell
`Action1/Action2/NextState`. Open the L1 one and find the row for `S` and the column
for `Store`.

The snooping L1's event vocabulary, exactly:

```
Load  Store  Replacement  Own_GetS  Own_GetM  Own_PutM
Other_GetS  Other_GetM  Other_PutM  OwnData  Invalidation  OwnData_Exclusive
```

## Two exercises — pick one

**A. Read the difference between MESI and MOESI.** Diff
`MESI_splitBus_snooping.csv` against `MOESI_splitBus_snooping.csv`. Find the `O`
state, list every transition into and out of it, and explain in one sentence what
the O state buys and what it costs. Then run both on `cacheb01` and check your
sentence against the response-bus column.

**B. Add a transient state.** Pick a race the table handles with a `Stall/` and
handle it with an explicit state instead. Add the state, add its row, run
`a2time01`, and confirm the result is identical (it should be — you have only
changed *how* the case is handled, not *what* happens).

## The trap — worth reading twice

`FSMReader` indexes transition rows **by position**, not by name. When you add a
state, its row must be appended **last**, so that its position matches the number
you gave it in the state list. Put it anywhere else and the FSM silently runs another
state's transitions — no error, just wrong results. This is the single most likely
way this exercise goes wrong.

Two smaller ones: the L1 and LLC tables are a **matched pair** (a MESI L1 with an MSI
LLC faults with "Wrong destination"); and on this branch protocols are switched by
preset, not by `-p` — see `01-exploration`.

## Checking

Both exercises end with a run whose `Summary.csv` you compare to a run *before* your
edit. Keep the before-copy:

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" >/dev/null 2>&1
cp $W/newLogger/Summary.csv /tmp/before.csv
# ... edit the FSM ...
./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" >/dev/null 2>&1
diff /tmp/before.csv $W/newLogger/Summary.csv && echo identical
```
