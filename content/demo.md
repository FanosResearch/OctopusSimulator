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

## Complete developer reference

The complete tutorial and developer reference is available in this site:

- [Architecture](tutorial/Architecture.md)
- [Logging and monitoring](tutorial/Logger.md)
- [Tracing](tutorial/Trace.md)
- [Debugger](tutorial/Debugger.md)
- [Visualizer](tutorial/Visualizer.md)
- [Tasks](tutorial/Tasks.md)
- [Adding a coherence protocol](tutorial/AddingAProtocol.md)
- [Message encoding](tutorial/MessageEncoding.md)
- [State and data](tutorial/StateAndData.md)
- [Interconnect and multi-hop design](tutorial/NoC_MultiHop_Plan.md)

These pages are part of the unified documentation and are intended to be
searchable alongside the stable and dev-stable usage guides.