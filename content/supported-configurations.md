# Supported configurations

Octopus exposes the memory hierarchy as a configuration space. The shipped
options include:

| Area | Available models |
| --- | --- |
| Coherence | Snoop and directory MSI, MESI, and MOESI; CSV FSM extension |
| Interconnect | Point-to-point, unified bus, split bus, triple bus, mesh, NoC |
| Arbitration | FCFS, FR-FCFS, round-robin, weighted round-robin, harmonic round-robin, TDM, GRROF |
| Cache hierarchy | Arbitrary depth, private/shared levels, multi-bank, non-blocking controllers |
| Replacement | LRU, FIFO, random |
| Predictability | Set and bank partitioning, predictable coherence protocols, worst-case tracking |
| Main memory | Fixed latency or cycle-accurate DRAM through MCsim |
| Integration | Standalone traces, gem5 Arm/Linux, MacSim, and MCsim |
| Monitoring | Per-stage latency, worst-case reports, filterable CSV traces, raw event traces |

The full capability matrix and protocol names are maintained in
[`SUPPORTED_CONFIGURATIONS.md`](https://github.com/FanosResearch/OctopusSimulator/blob/stable/SUPPORTED_CONFIGURATIONS.md).
Use that file as the authoritative list when designing an experiment.

## Configuration selection

System CSVs live under `configuration/SystemConfigurations/`. Select one by name
or path:

```bash
./build/Octopus_Simulator -s MultiCoreSystem -c MultiCoreSystem_Directory
```

Override individual values without changing the shipped file:

```bash
./build/Octopus_Simulator -s MultiCoreSystem \
  -c MultiCoreSystem_Directory \
  -p "num_cores(i)=4"
```

Overrides are applied after the selected system file is loaded. This makes
configuration-only sweeps reproducible and leaves the repository presets intact.