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

## Usage
```bash
# one axis, full EEMBC suite
bash sweeps/sweep_arbiter.sh

# quick smoke on a single benchmark
BENCH=a2time01-trace bash sweeps/sweep_memory.sh

# all axes
bash sweeps/sweep_all.sh

# SPLASH instead of EEMBC (once SPLASH traces are present)
SUITE=splash bash sweeps/sweep_cache.sh
```
Env knobs: `SUITE=eembc|splash` (default `eembc`), `SAFETY=<sec>` per-run cap
(default 300), `BENCH=<dir>` to run a single benchmark.

Results are written to `results/<axis>/results.csv` with columns
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
- RR/TDM bus arbiters require the fix that includes the LLC in the arbiter
  candidate list (commit `ad46e150`); without it they starve LLC responses and
  hang.
