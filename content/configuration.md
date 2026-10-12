# Configuration model

Configuration is hierarchical and CSV-based. A component can inherit defaults,
override selected values, and pass parameters to child components. Command-line
`-p` values are applied after the files are loaded.

The main locations are:

| Directory | Contents |
| --- | --- |
| `configuration/` | Component and system parameters |
| `configuration/SystemConfigurations/` | Top-level system presets |
| `Protocols_FSM/` | Coherence state machines |
| `sweeps/` | Configuration-only experiment drivers |

## A repeatable edit loop

1. Copy or select a system configuration.
2. Change one parameter, or supply one `-p` override.
3. Run the same workload and output to a new directory.
4. Compare `Summary.csv` and the per-request reports.
5. Record the selected config and command with the result.

For a complete command reference, use the source repository's
[StateAndData.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/StateAndData.md)
and [MessageEncoding.md](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/MessageEncoding.md).