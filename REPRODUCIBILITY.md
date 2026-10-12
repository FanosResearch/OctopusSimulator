# Reproducing the Results

This guide takes a reviewer from a fresh clone to the figures and tables in the
paper. **Every experiment is a configuration-only change to a single build** of
Octopus — no source edits between runs — which is the tool's central claim.

For the internal design of the sweep harness, see
[`sweeps/README.md`](sweeps/README.md); for the full capability matrix see
[`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md).

---

## 1. Prerequisites

- A **C++17** toolchain and **CMake** (g++ ≥ 9 or clang; also builds under MinGW
  on Windows).
- **Python 3** with **matplotlib** for the figures: `pip install matplotlib`
- **Bash** (the driver scripts are POSIX bash).
- **xz** (`xz-utils`) to inflate the compressed SPLASH-2 traces (preinstalled on
  most Linux distributions; part of MSYS2/MinGW on Windows).

## 2. Build (one command)

```bash
mkdir -p build && cd build && cmake .. && make -j
```

This produces the simulator binary `build/Octopus_Simulator` (named
`Octopus_Simulator.exe` under Windows/MinGW).

## 3. Smoke test — a single benchmark

```bash
./run_octopus.sh --protocol snoop --suite eembc --bench a2time01-trace
```

Runs in seconds and prints per-core latency reports plus a completion verdict.
`run_octopus.sh --help` lists all options (`--protocol snoop|directory`,
`--suite eembc|splash`, `--bench`, `--jobs`, `--safety`, `--out`).

## 4. Benchmarks included

- `BMs/eembc-traces/` — EEMBC automotive traces, run on every core to create a
  data-sharing stress workload.
- `BMs/splash/` — SPLASH-2 application traces (including the `raytrace` and
  `radiosity` "giants").

### The benchmarks are a separate repository

The traces are **not in this repository** (the SPLASH-2 set alone is ~10 GB
uncompressed). They live in
[`FanosResearch/OctopusBMs`](https://github.com/FanosResearch/OctopusBMs), where
the SPLASH-2 traces are stored as **xz archives** (~650 MB total; every archive is
under GitHub's 100 MB limit, and the tooling also reassembles split `.xz.part-NN`
pieces should a future trace need them). **Nothing to do by hand:** every driver (`run_octopus.sh`, `run_splash.sh`, `sweep_protocols.sh`,
and the `sweeps/` scripts) first calls

```bash
./get_benchmarks.sh      # clone OctopusBMs into BMs/ if absent, then inflate
```

which clones the benchmark repository into `BMs/` on first use (skipped when
`BMs/` is already a clone) and then runs `BMs/prepare_traces.sh` to reassemble
and decompress the archives *in place*. Both steps are idempotent — an existing
clone and existing traces are skipped — so it is cheap to run every time. Run it
yourself only if you invoke the simulator binary on a benchmark directly. Budget
**~10 GB of disk** for the inflated traces. `BMs/` is git-ignored here.

## 5. Reproduce the configuration sweeps

Each script varies **one** axis over a fixed baseline (snoop **MESI**, **FCFS**
bus, **LRU**, shared LLC, fixed-latency memory) and records, per
`(config, benchmark)`, completion status plus latency metrics:

```bash
bash sweeps/sweep_arbiter.sh        # bus arbiter:      FCFS / RR / TDM
bash sweeps/sweep_cache.sh          # LLC capacity:     16 / 32 / 64 KB
bash sweeps/sweep_replacement.sh    # replacement:      LRU / RANDOM
bash sweeps/sweep_memory.sh         # main memory:      fixed-latency / MCsim DDR4
bash sweep_protocols.sh             # coherence:        MSI/MESI/MOESI x snoop/directory
```

Each writes `results/<axis>/<suite>.csv`. Useful environment knobs
(see `sweeps/README.md`):

```bash
SUITE=eembc|splash      # which suite (default eembc)
JOBS=<N>                # parallel benches per config (default: nproc-2)
SAFETY=<sec>            # per-run wall-clock cap
BENCH=<name>            # single benchmark
EXCLUDE="<names>"       # skip benches, e.g. EXCLUDE="raytrace radiosity"
```

Run the **entire matrix across both suites** in one shot:

```bash
bash sweeps/run_overnight.sh        # progress + summary in results/overnight.log
```

The **SPLASH-2 baseline** used for the paper's table:

```bash
./run_octopus.sh --protocol snoop --suite splash --out results/splash_baseline
```

## 6. Regenerate the figures

```bash
python sweeps/plot_results.py       # results/*/*.csv  ->  results/figures/*.{png,pdf}
```

## 7. Paper artifact map

| Paper element | Produced by | Output file |
|---|---|---|
| **Fig. 3** — per-request stage breakdown | all EEMBC axis sweeps, then `plot_results.py` | `results/figures/fig1_stage_breakdown.*` |
| **Fig. 4** — mean vs. worst-case by arbiter | `sweeps/sweep_arbiter.sh` (EEMBC), then `plot_results.py` | `results/figures/fig4_wcet_vs_avg.*` |
| **Table 5** — SPLASH-2 baseline | `run_octopus.sh --protocol snoop --suite splash` | `results/splash_baseline/baseline.csv` |
| **Table 4** — lines of code per protocol | reported from the Octopus CAL paper | — |

`plot_results.py` also emits `fig2_cache_capacity`, `fig3_memory_dram`,
`fig5_normalized`, `fig6_config_heatmap`, and `fig7_status_matrix` from the same
CSVs, for readers who want the other axes.

## 8. Runtime expectations

| Scope | Rough wall-clock |
|---|---|
| One-time benchmark fetch + inflate (`get_benchmarks.sh`) | clone ~650 MB, then ~1 minute to inflate (~10 GB written) |
| Single EEMBC benchmark | seconds |
| One EEMBC axis (all benches, parallel) | minutes |
| SPLASH-2 (non-giant benches) | minutes |
| SPLASH-2 giants (`raytrace`, `radiosity`) | hours each |
| MCsim memory axis | EEMBC-only (cycle-accurate DDR4 is ~5–10× slower) |

## 9. Notes

- Octopus reads its **entire configuration once at process startup** from a
  compile-time absolute path, so config changes are **serial** while benchmarks
  **within** a config run in **parallel** — the sweep drivers exploit exactly this.
- Completion is judged **per core**: each core's report must cover its own trace
  *and* carry the end-of-simulation summary footer (SPLASH distributes work
  unevenly across cores).
- Developed and tested on Ubuntu 18.04 / 20.04 and Windows (MinGW).

### If a script works on Windows but not on Linux
Two differences account for almost every case, and both are handled by the scripts now:

- **`cygpath`.** On Windows the simulator is a native binary, so a `/c/...` shell path has to be
  converted before it is passed as `workload_path`. On Linux that command does not exist. Every
  call is now guarded (`cygpath -m "$p" 2>/dev/null || printf '%s' "$p"`, or `winpath` from
  `sweeps/sweep_common.sh`); an unguarded one leaves the path **empty**, and the run is handed
  `workload_path(s)=/` — it starts and then finds no traces. If you add a script, use `winpath`.
- **`python` vs `python3`.** Many distributions ship only `python3`. The scripts pick the first of
  `python3`, `python`, `py` that actually runs, so nothing needs to be set; `PY=/path/to/python`
  still overrides.

Everything else the scripts use (`sed -i`, `stat -c`, `timeout`, `nproc`, `mktemp`, `wait -n`,
`md5sum`) is GNU coreutils and present on Linux. Run them with `bash script.sh`, not `sh`.
If a run still fails on Linux, the first thing to look at is `<workload>/.sweep.stderr` (or
`.out`/`.stderr` next to the workload), which holds the simulator's own message.
