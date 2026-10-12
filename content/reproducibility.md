# Reproducibility

Octopus experiments are intended to vary configuration, not source code. A
reproducible run records the commit, configuration, workload suite, command,
output directory, and toolchain.

## Baseline workflow

```bash
mkdir -p build
cmake -S . -B build
cmake --build build -j
./run_octopus.sh --protocol snoop --suite eembc --bench a2time01-trace
```

The sweep scripts vary one axis at a time and write CSV results under `results/`:

```bash
bash sweeps/sweep_arbiter.sh
bash sweeps/sweep_cache.sh
bash sweeps/sweep_memory.sh
bash sweep_protocols.sh
python3 sweeps/plot_results.py
```

Useful environment variables include `SUITE`, `JOBS`, `SAFETY`, `BENCH`, and
`EXCLUDE`. Read the source
[REPRODUCIBILITY.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/REPRODUCIBILITY.md)
before starting a large SPLASH-2 run.

## Benchmark storage

The benchmark repository is external and the decompressed SPLASH-2 traces need
roughly 10 GB. Driver scripts call `get_benchmarks.sh` automatically; the helper
is idempotent, so it is safe to invoke repeatedly.

## Report a reproducibility issue

Include the commit, host OS, compiler, exact command, selected CSVs, and the
first differing report. The fixed environment check is the fastest first
diagnostic.