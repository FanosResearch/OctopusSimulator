# Octopus Simulator

Octopus is a cycle-accurate cache system simulator for multicore memory-system
research. It models cache controllers, coherence protocols, configurable
interconnects, arbiters, main memory, and monitoring in one modular C++17
framework.

The project is designed for **configuration-driven experiments**: hierarchy
parameters live in CSV files, coherence protocols are CSV finite-state machines,
and command-line overrides let you run a design-space sweep without editing C++.
These pages are maintained for the `stable` and `dev-stable` development lines;
commands that depend on another branch are not part of the supported workflow.

## Start here

| Goal | Page |
| --- | --- |
| Build and check the simulator | [Install and verify](quick-start.md) |
| Run a first workload | [Run a benchmark](run-a-benchmark.md) |
| See timing effects visually | [Demo and visualizer](demo.md) |
| Understand the design | [Architecture](architecture.md) |
| Add or modify coherence behavior | [Add a coherence protocol](add-a-protocol.md) |
| Reproduce published experiments | [Reproducibility](reproducibility.md) |
| Integrate with gem5 and inspect results | [gem5 integration](gem5-integration.md) |
| Search the complete developer reference | [Tutorial and developer reference](tutorial/Architecture.md) |
| Cite Octopus | [Publications and citation](publications.md) |

## Why Octopus?

Octopus combines a clocked component model with hierarchical CSV configuration.
It can represent private and shared cache levels, multiple interconnect
topologies, several arbitration policies, fixed-latency memory, and cycle-accurate
MCsim memory. The Logger records stage-level latency and the Debugger provides
filterable component and coherence traces.

```mermaid
flowchart LR
    accTitle: Octopus research workflow
    accDescr: A workload is run through a configured multicore memory hierarchy, producing latency reports, traces, and reproducible figures.
    workload[Workload] --> simulator[Configured Octopus run]
    simulator --> reports[Latency reports]
    simulator --> trace[Optional event trace]
    reports --> analysis[Analysis and figures]
    trace --> visualizer[Octoviz timeline]
```

## Project status

The stable branch is the source of the main simulator documentation. Benchmark
traces are maintained in the separate
[OctopusBMs repository](https://github.com/FanosResearch/OctopusBMs) and are
fetched only when a benchmark run needs them.

!!! note
    The complete tutorial and developer reference is published in this site under
    [Tutorial and developer reference](tutorial/Architecture.md). The pages cover
    architecture, messages, state and data, logging, tracing, debugging,
    visualization, tasks, interconnect plans, and protocol extension.

## Citation

If Octopus supports published work, cite the simulator paper listed on
[Publications and citation](publications.md). The project also maintains a list
of research that uses or extends Octopus.