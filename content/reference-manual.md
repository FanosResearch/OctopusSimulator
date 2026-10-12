# Reference manual

The [Octopus reference manual](reference/octopus-manual.pdf) is the book-form
version of this documentation. It includes the architecture, cache controller,
LLC datapath, coherence engine, request path, interconnects, main memory,
configuration, monitoring, tasks and demo, extension points, gem5 integration,
evaluation, configuration keys, FSM tables, CLI reference, and file formats.

The manual source and build instructions are maintained in the simulator
repository's [`docs/manual/`](https://github.com/FanosResearch/OctopusSimulator/tree/stable/docs/manual)
directory. Use the website pages in the **Developer reference** section for
searchable Markdown, and use the PDF when a single paginated reference is
preferred.

## Chapter coverage

| Topic | Web documentation |
| --- | --- |
| System architecture and data flow | [Architecture](tutorial/Architecture.md) |
| Cache messages and state | [Message encoding](tutorial/MessageEncoding.md), [State and data](tutorial/StateAndData.md) |
| Logging, tracing, and debugging | [Logger](tutorial/Logger.md), [Trace](tutorial/Trace.md), [Debugger](tutorial/Debugger.md) |
| Visualization and tasks | [Visualizer](tutorial/Visualizer.md), [Tasks](tutorial/Tasks.md) |
| Coherence protocol extension | [Add a protocol](tutorial/AddingAProtocol.md) |
| Interconnect design | [Interconnect multi-hop plan](tutorial/NoC_MultiHop_Plan.md) |
| gem5 and full-system integration | [gem5 integration](gem5-integration.md) |
