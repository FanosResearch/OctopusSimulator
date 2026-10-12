# Interconnects and memory

Interconnect behavior is split into a topology, a controller, and an arbiter.
This lets the same cache and protocol components run over a bus, point-to-point
links, a mesh, or a NoC while changing only the relevant configuration.

The memory endpoint can be a simple fixed-latency model for fast design-space
exploration or an MCsim-backed controller for cycle-accurate DRAM studies.

## Design questions to answer first

| Question | Configuration area |
| --- | --- |
| Which cores and cache levels exist? | System and cache controller CSVs |
| How do components connect? | `configuration/Interconnect/` |
| How is shared traffic selected? | Arbiter type and parameters |
| Which coherence transitions occur? | `Protocols_FSM/` and protocol type |
| Which memory timing is modeled? | Main-memory or MCsim configuration |
| Where are results written? | `-o` output directory |

The source repository contains implementation details and configuration examples
for each component. Begin with
[Architecture.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/Architecture.md),
then inspect the matching CSV preset before changing a run.