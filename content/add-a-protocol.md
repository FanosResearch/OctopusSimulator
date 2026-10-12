# Add a coherence protocol

Coherence protocols in Octopus are CSV finite-state machines. For protocols that
use the existing protocol handler and actions, adding a protocol requires a CSV
and a configuration selection, not a C++ change or a rebuild.

## Example: MI

The repository's worked example adds MI, with only Invalid and Modified stable
states, by adapting the existing snooping MSI handler. Its FSM is stored in
`Protocols_FSM/MI_splitBus_snooping.csv`.

Select it with a command-line override:

```bash
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=BMs/eembc-traces/mi_pingpong/" \
  -p "cache_controller[*].fsm_filename(s)=MI_splitBus_snooping"
```

The CSV cells describe actions and next states. Every event used by the workload
must have a defined transition, including transient states and responses.

## A practical validation loop

1. Copy an existing protocol CSV with the same message vocabulary.
2. Define stable and transient states and fill every required event column.
3. Run a small workload that exercises reads, writes, misses, and sharing.
4. Enable the Debugger for one address and inspect transitions.
5. Compare Logger results against a known protocol on the same workload.

The full MI walkthrough, expected transitions, and example cost comparison are in
[`docs/AddingAProtocol.md`](https://github.com/FanosResearch/OctopusSimulator/blob/stable/docs/AddingAProtocol.md).