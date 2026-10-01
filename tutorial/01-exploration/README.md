# 01 — Exploration: one axis at a time

**Goal:** convince yourself the axes really are independent. Change one thing,
re-run (a few seconds), watch one number move. About 25 minutes.

Use this shell helper for the whole exercise. It runs `a2time01` with whatever
overrides you pass and prints the three numbers worth comparing:

```shell
W=$PWD/BMs/eembc-traces/a2time01-trace        # Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/a2time01-trace")
run(){ ./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" "$@" >/dev/null 2>&1 \
       && awk -F, 'NR==2{print "  worst total="$10"  worst DRAM="$8"  finish="$13}' $W/newLogger/Summary.csv; }
```

Then, one axis at a time:

```shell
echo FCFS;  run -p "bus[0].interconnect_controller.arbiter_type(s)=FCFSArbiter"
echo RR;    run -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter"
echo TDM;   run -p "bus[0].interconnect_controller.arbiter_type(s)=TDMArbiter"
echo MCsim; run -p "main_memory_type(s)=MCsim" -p "mcsim_scheduler(s)=FRFCFS"
echo MSHR4; run -p "cache_controller[*].num_mshr(i)=4"
echo part;  run -p "llc_controller.m_data_handler.way_partition(s)=0:0;1-3:1"
```

Fill in the table as you go. The point is not the numbers — it is that each row
differs from the last in exactly one parameter.

The `part` row is the one with a story behind it: under a shared inclusive LLC a
streaming core's fills evict a quiet core's lines, and by inclusion its L1 copies too,
so the quiet core's next accesses go to DRAM through no fault of its own. The sequence
is drawn in [`docs/imgs/inclusion_interference.svg`](../../docs/imgs/inclusion_interference.svg);
way reservation is what stops it, and no bus arbiter can.

| change | worst total | worst DRAM | finish cycle |
|---|---|---|---|
| as shipped (TDM, fixed latency) | | | |
| FCFS bus | | | |
| RR bus | | | |
| MCsim DDR4, FR-FCFS | | | |
| L1 MSHR = 4 | | | |
| LLC: 1 way reserved for core 0 | | | |

## The axes that ship

| axis | parameter | values |
|---|---|---|
| bus arbiter | `bus[0].interconnect_controller.arbiter_type` | `FCFSArbiter` `RRArbiter` `TDMArbiter` |
| LLC arbiter | `llc_controller.arbiter_type` | same |
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

Run the same six configurations on `cacheb01-trace` — the benchmark where all four
cores fight over one block. The arbiter rows spread out much more.

`sweeps/` holds the scripts that sweep these axes in bulk and produce the figures in
the papers; `sweep_protocols.sh` is the readable entry point.
