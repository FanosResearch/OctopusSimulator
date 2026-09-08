# Configuration-axis sweeps

Each script varies **one** Octopus configuration axis against a fixed baseline
and records, per `(config, benchmark)`, whether the run **completes** plus
latency metrics parsed from the Logger's `Summary.csv` (see
[`../docs/Logger.md`](../docs/Logger.md)). This demonstrates that a single
config change — **no recompilation** — measurably changes system behavior.

**Baseline** (held fixed while one axis varies): snoop **MESI**, TripleBus,
L1-private + shared LLC, **LRU** replacement, **FCFS** bus arbiter,
**MainMemory** (fixed-latency), default cache geometry.

| script | axis | values | knob location | headline metric |
|--------|------|--------|---------------|-----------------|
| `sweep_arbiter.sh` | bus arbiter | FCFS, RR, TDM | `configuration/Interconnect/SplitBusController.csv` | wcReqBus / wcRespBus |
| `sweep_replacement.sh` | replacement | LRU, RANDOM | system CSV | wcTotal / avg |
| `sweep_cache.sh` | LLC capacity | 16K / 32K / 64K | system CSV | wcTotal / finish |
| `sweep_memory.sh` | main memory | MainMemory, MCsim (DDR4) | system CSV | wcDRAM / avg |
| *(coherence)* | protocol × family | MSI/MESI/MOESI × snoop/dir | `../sweep_protocols.sh` | wcTotal |

## Parallelism

Octopus reads its entire config **once at process startup** from a fixed
*absolute* path (`CONFIGURATION_PATH` is baked at compile time), so two runs with
**different** configs cannot share the tree. Therefore:

- **Config changes are serial.** Between axis values there is a barrier.
- **Benches within one config run in parallel** — they are independent
  processes writing to distinct `newLogger/` dirs, and the on-disk CSV is
  constant across the batch. `run_axis` fans out up to `JOBS` at once
  (default `nproc-2`), **largest-trace-first** for better packing, writing each
  result to a part file and assembling the CSV when the axis finishes.
- Because config mutation only affects a process at *startup*, a long run
  launched earlier is immune to later CSV edits (used by the overnight driver).

## Overnight driver

`run_overnight.sh` runs the whole matrix across **both suites**:

```bash
bash sweeps/run_overnight.sh              # EEMBC (all axes) + SPLASH (MainMemory axes) + giants
```
- **EEMBC**: all 4 axes (MCsim completes quickly here).
- **SPLASH**: arbiter / replacement / cache only. The **memory axis is
  EEMBC-only** — MCsim's cycle-accurate DDR4 is ~5–10× slower and TIMEOUTs even
  on the smallest SPLASH bench.
- **SPLASH giants** (`raytrace` 44M, `radiosity` 72M lines) would cost ~13 h if
  run under all 10 axis values, so they run **once under baseline**, concurrently
  with the sweep (their processes have already parsed the baseline CSVs).
- Progress + a final status summary go to `results/overnight.log`.

Env overrides: `JOBS`, `EEMBC_AXES`, `SPLASH_AXES`, `GIANTS`,
`EEMBC_SAFETY` / `SPLASH_SAFETY` / `GIANT_SAFETY`.

## Usage
```bash
# one axis, full EEMBC suite (parallel)
bash sweeps/sweep_arbiter.sh

# quick smoke on a single benchmark
BENCH=a2time01-trace bash sweeps/sweep_memory.sh

# SPLASH instead of EEMBC, skipping the two giants
SUITE=splash EXCLUDE="raytrace radiosity" bash sweeps/sweep_cache.sh

# cap fan-out
JOBS=8 bash sweeps/sweep_cache.sh
```
Env knobs: `SUITE=eembc|splash` (default `eembc`), `JOBS` (default `nproc-2`),
`SAFETY=<sec>` per-run cap, `BENCH=<dir>` single benchmark,
`EXCLUDE="<names>"` skip benches.

Results are written to `results/<axis>/<suite>.csv` (suite-aware: `eembc.csv`
vs `splash.csv`, no clobber) with columns
`value,benchmark,status,avg,wcTotal,wcReqBus,wcRespBus,wcDRAM,finish`.

## Metrics
From each run's `newLogger/Summary.csv` (per-core worst-case + average):
`avg` (mean effective latency), `wcTotal` (worst-case end-to-end), `wcReqBus` /
`wcRespBus` / `wcDRAM` (worst-case per-stage), `finish` (last-retire cycle =
runtime). The per-axis component shows *where* the knob acts.

## Notes / gotchas
- **Preset CSVs have no trailing newline** — appended overrides are
  newline-guarded in `sweep_common.sh::set_csv`; a bare `echo >>` would glue the
  line onto a comment and be silently ignored.
- The **arbiter** is set in the interconnect *Extends* file, not the system CSV
  (`sweep_arbiter.sh` backs it up and restores it on exit).
- Under `set -u`, declare `local b="$1"; local wp="$TR/$b"` on **separate**
  lines — a single `local` expands `$b` before assigning it.
