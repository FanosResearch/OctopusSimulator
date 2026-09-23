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
| `sweep_pcc_ab.sh` | perfect vs real LLC (PCC-like: RR bus, RR LLC, MCsim/FRFCFS DRAM) | `perfect_llc` 1 / 0 | system CSV + `SplitBusController.csv` | per-stage wc (where DRAM leaks into a *hit*) |
| `sweep_pcc_par.sh` | same A/B × core OoO window, **all cells in parallel** (per-run knobs via `-p`, hardlinked per-run dirs; see the header) | `perfect_llc` {2 = PCC-perfect (no misses), 1 = zero-latency memory, 0 = real} × `OoO` {1,8} | shared config once + `-p` per run | per-stage wc, hit-only via `analyze_pcc_par.sh` |
| `analyze_mechanisms.sh` | aggregates the Logger's per-request mechanism trackers over `sweep_pcc_par.sh` runs: request classes by LLC arrival state (L1 hit / stable / transient / coalesced / miss) with worst-case Total, Oldest, L2-Stall, L2-Access, Response-Bus per class; younger own responses granted ahead (and refills among them); array-port accesses ahead (and writes) | — | rows under `results/pcc_par/wl_<suite>/` | `results/pcc_par/mechanisms_<suite>.csv` |
| `trace_pcc.sh` | the `sweep_pcc_par.sh` cells re-run **with the raw event trace** (`OCTOPUS_TRACE`, docs/Trace.md) and converted (`convert.py --trace`, streamed) for the visualizer and trace-based analyses; same shared config written the same way, restored on exit; `TRACE_WINDOW=t0:t1` for giant runs | `perfect_llc` × `OoO` as env lists | shared config once + `-p` per run | `results/pcc_par/trace_<suite>.csv` (status, trace size, validation), rows + Parquet under `results/pcc_par/wl_trace_<suite>/` |
| `analyze_trace_ahead.py` | from the trace: what sat ahead of each waiting request at the LLC array port (own/other reads, write-backs, fills, evictions, cut-ins) and on the response bus (own/other responses split hit/miss, supplies, write-backs), worst case + mean for oldest hits and misses, cross-checked against the Logger counters | — | `results/pcc_par/wl_trace_<suite>/` | `results/pcc_par/trace_ahead_<suite>.csv` + per-run `ahead_array.csv` / `ahead_resp.csv` |
| `split_hits.sh` | (older, window-based) independent-vs-coalesced split from time-resolvable rows; superseded by the `LLC Arrival State` column — kept for cross-checking | — | one `newLogger` dir | stdout |

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
`value,benchmark,status,avg,wcTotal,wcEff,wcL1stall,wcReqBus,wcL2stall,wcL2access,wcRespBus,wcDramBus,wcDRAM,wcL1access,finish,wcOldest`
(`METRIC_HEADER` in `sweep_common.sh`; `wcL1access` is the Logger's return-path
stage, so the seven stage columns tile to Total; `wcOldest` is the worst head-of-queue latency, PCC's per-request quantity, equal to `wcTotal` at OoO=1).

## Metrics
From each run's `newLogger/Summary.csv` (per-core worst-case + average):
`avg` (mean effective latency), `wcTotal` (worst-case end-to-end), `wcReqBus` /
`wcRespBus` / `wcDRAM` (worst-case per-stage), `finish` (last-retire cycle =
runtime). The per-axis component shows *where* the knob acts.

## Notes / gotchas
- **Benchmarks live in a separate repository** (`FanosResearch/OctopusBMs`; the
  SPLASH-2 set is ~10 GB, over GitHub's per-file limit, so it is stored
  compressed there). `sweep_common.sh` calls `../get_benchmarks.sh "$TR"` right
  after selecting the suite: it clones the repo into `BMs/` if absent and
  inflates the traces, so the plain `trace_C*.trc.shared` files exist before
  `benches()` discovers them. Both steps are idempotent, so it costs nothing
  once done. See `../REPRODUCIBILITY.md`.
- **Preset CSVs have no trailing newline** — appended overrides are
  newline-guarded in `sweep_common.sh::set_csv`; a bare `echo >>` would glue the
  line onto a comment and be silently ignored.
- The **arbiter** is set in the interconnect *Extends* file, not the system CSV
  (`sweep_arbiter.sh` backs it up and restores it on exit).
- **`gen_baseline` copies the `MultiCoreSystem_Snoop.csv` preset over the active
  system CSV**, so any sizing that only lives in the committed
  `MultiCoreSystem.csv` is silently lost for every sweep run. The snoop bus
  back-pressure sizing (`bus[*].buffers_max_size 256` + `bus[0].response_reserve
  128`, sized to the MSHR bound) now lives in the preset too; with the preset's
  old `32`/no-reserve the response TX buffer could overflow (`Cannot insert the
  Msg into BusTxResp FIFO`, an `exit(0)` mid-run that leaves an INCOMPLETE
  status with no footer). Keep the preset and the committed system CSV in sync.
- Under `set -u`, declare `local b="$1"; local wp="$TR/$b"` on **separate**
  lines — a single `local` expands `$b` before assigning it.
- **Config mutations are verified.** On Windows a running simulator keeps the
  config file open, so `sed -i` (temp-file + rename) can fail **silently** and
  leave the un-edited MSI preset — making a sweep run the wrong
  protocol/controller/arbiter without any error. `gen_baseline`, `set_csv`, and
  `set_arbiter` now kill stray sims, retry, and **verify the edit landed,
  aborting loudly if it cannot** — so a sweep can never silently produce the
  wrong config.
