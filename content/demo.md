# Demo and visualizer

Octopus includes a local tracking demo that turns simulated job timing into a
trajectory. It shows how shared-cache and DRAM interference affects a periodic
real-time task, and how reserving cache ways can recover the planned behavior.

The demo is in `demo/` on the stable branch:

```bash
cd demo
bash run_acts.sh
```

The generated reports and figures are kept under the demo output directories.
Read `demo/README.md` for the exact acts and plotting options.

## Octoviz timeline viewer

For a run that has a `newLogger/` directory, view the request pipeline locally:

```bash
./octoviz.sh view <workload-directory>
```

Or convert and serve existing runs:

```bash
./octoviz.sh convert results
./octoviz.sh serve results
```

The viewer needs Python 3 with `duckdb` and `numpy`:

```bash
python3 -m pip install duckdb numpy
```

It provides a request pipeline, shared-resource lanes, an inspector, and a
coherence transition table. Add `--trace` to the simulator run to include
non-request traffic such as invalidations, write-backs, and fills.

## Hands-on tutorial

The structured tutorial lives on the `esweek-tutorial` branch because it is not
part of every stable checkout:

```bash
git clone -b esweek-tutorial https://github.com/FanosResearch/OctopusSimulator.git octopus-tutorial
cd octopus-tutorial/tutorial
```

Start with `tutorial/00-setup-and-first-run/`, then continue through exploration,
configuration, extension, and gem5 integration. Treat the branch name as part of
the instructions; the stable branch does not promise the tutorial tree.