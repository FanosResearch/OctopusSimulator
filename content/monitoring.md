# Monitoring

Octopus has two complementary monitoring facilities and an optional raw trace.

## Logger

The Logger passively records each tracked request and decomposes its lifetime into
stages such as L1 stall, request bus, LLC, memory bus, DRAM, response bus, and
L1 access. It produces per-core reports, averages, worst-case values, and a
cross-core summary.

The Logger does not change simulation timing. Read the detailed source reference
in [`docs/Logger.md`](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/Logger.md).

## Debugger

The Debugger is opt-in and component-local. It can print general events,
message-associated rows, or readable coherence transitions. Filters can select
addresses, message IDs, sources, and owners, making it useful for diagnosing one
cache line or one protocol transition.

The detailed options are documented in
[`docs/Debugger.md`](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/Debugger.md).

## Raw trace

Use `--trace` to write `trace.bin` and its names file to the output directory.
The trace includes traffic and FSM information that is not represented by request
rows alone. `octoviz.sh` can convert it into resource lanes and a per-line
coherence transition table. See the source references for
[Trace.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/Trace.md)
and [Visualizer.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/Visualizer.md).