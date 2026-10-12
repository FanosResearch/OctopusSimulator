# Run a benchmark

The driver scripts handle benchmark preparation, configuration selection, and
report locations. The shortest useful run is one EEMBC trace:

```bash
./run_octopus.sh --protocol snoop --suite eembc --bench a2time01-trace
```

Use `./run_octopus.sh --help` for all options. Results include per-core latency
reports, summaries, and a completion verdict.

## Run the binary directly

After fetching benchmark traces, select a system configuration and workload:

```bash
./build/Octopus_Simulator \
  -s MultiCoreSystem \
  -p "workload_path(s)=BMs/eembc-traces/a2time01-trace/"
```

Write reports to a dedicated directory and record a raw event trace:

```bash
./build/Octopus_Simulator \
  -s MultiCoreSystem \
  -o results/first-run \
  --trace \
  -p "workload_path(s)=BMs/eembc-traces/a2time01-trace/"
```

`-s` selects the simulator system class. `-c` or `--config` selects a system CSV,
and `-p` overrides parameters after the CSV is loaded:

```bash
./build/Octopus_Simulator \
  -s MultiCoreSystem \
  --config MultiCoreSystem_Directory \
  -p "num_cores(i)=4"
```

## Understand the output

With `-o results/first-run`, expect files such as:

| File | Purpose |
| --- | --- |
| `Summary.csv` | Cross-core summary in the stable script format |
| `Summary_transposed.csv` | The same metrics with cores as columns |
| `LatencyReport_C*.csv` | Per-request and per-stage timing |
| `trace.bin` | Raw events when `--trace` is enabled |
| `config.log` | Resolved configuration when `--PrintConfig` is enabled |

Relative workload paths in shipped system CSVs resolve from the project root.
Output paths supplied with `-o` resolve from the directory where the command is
run.