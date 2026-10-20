# Supported Configurations and Features

Octopus models the **whole memory hierarchy** and exposes every layer as a
configuration choice or an editable CSV finite-state machine — no recompilation.
This matrix summarizes what ships today; every axis is also extensible.

Configuration is hierarchical (CSV files + command-line overrides), so any cell
below can be selected, swept, or combined without touching C++.

*(Compiled from the Octopus feature tables in the IEEE CAL and ECRTS papers.)*

---

## At a glance

| Axis | Ships with |
|---|---|
| Coherence protocols | Snoop & directory MSI / MESI / MOESI (CSV FSMs, stable + transient states); extensible incl. time-based coherence |
| Interconnect topology | Point-to-point, unified bus, split bus, triple bus, mesh, NoC, multi-hop flit-level network (grid, torus, ring, star, custom) |
| Arbitration | FCFS, FR-FCFS, RR, Weighted-RR, Harmonic-RR, TDM, GRROF |
| Cache hierarchy | Arbitrary depth, private/shared, multi-bank, non-blocking (MSHRs, WB/refill buffers) |
| Replacement | LRU, FIFO, Random |
| Predictable caching | Set partitioning, bank partitioning, predictable coherence protocols |
| Main memory | Fixed-latency model, or cycle-accurate DRAM via MCsim |
| Operating modes | Standalone (trace-driven), full-system (gem5 Arm/Linux, MacSim, MCsim) |
| Monitoring | Per-request per-stage latency, worst-case tracking, filterable CSV traces |

---

## Coherence protocols

- **Snooping-based:** MSI, MESI, MOESI
- **Directory-based:** MSI, MESI, MOESI
- FSM-based protocol handlers with **stable and transient states**, authored as
  **CSV tables** and interpreted at startup (no recompilation to add or modify a
  protocol)
- Extensible to new protocols, including **time-based cache coherence**
- **Predictable coherence protocols already implemented:** PMSI, PMESI,
  PMSI\*, PMESI\*, PENDULUM, PISCOT, DISCO, PCC, DUPECO, CoHoRT
  (see [`PUBLICATIONS.md`](PUBLICATIONS.md))

## Interconnect topology

- Point-to-point (direct link)
- Unified bus
- Split bus
- Triple bus
- Mesh
- Network-on-Chip (NoC)
- Configurable interconnect controllers and topology (connection) maps for custom
  topologies
- Multi-hop network (below)

### Multi-hop network (`interconnect_type Network`, system `MultiCoreSystem_Mesh`)

A flit-level wormhole network with credit-based flow control. Specification:
[`docs/NoC_Architecture_Spec.md`](docs/NoC_Architecture_Spec.md); keys and defaults:
`configuration/Interconnect/Network.csv`.

| Aspect | Options |
|---|---|
| Topology | `grid2d`, `torus2d`, `ring`, `star`, `custom` (explicit routers and links); endpoints attach to routers through `node_map` |
| Routing | `XY`, `Table` (shortest paths), `WestFirst`, `AdaptiveMinimal` (with an escape VC); `auto` = XY on grids, Table otherwise |
| Virtual networks | `num_classes 3` (request / forward / response) or 2 (legacy request / data), `vcs_per_class`, `vc_depth` |
| Timing | `router_latency`, `link_latency`, per-class link latency, `flit_bytes`, `header_bytes`, pipelined or serial links |
| Allocation | FCFS or RR for VC and switch allocation |
| Endpoint queues | `inject_queue_depth`, `eject_queue_depth`; controllers reserve transmit slots before a transition, so a push is never refused |
| Deadlock freedom | routing-level check and a protocol-and-network check of the FSM tables at construction (`deadlock_check refuse \| warn`) |
| LLC banking | `llc_banks`, `home_ids`, `home_interleave_shift`: several LLC banks as directory homes, lines interleaved |
| Memory on the network | `memory_on_network 1`: memory controllers are endpoints; each LLC bank reaches memory through its memory port (`mem_port_id`); several controllers with `mem_ids` / `mem_interleave_shift`; fixed-latency memory or MCsim (one instance per controller) |
| Observability | per-request NoC Hops / Wait / Transfer / Ahead columns (and NoC Mem columns with memory on the network), `NetworkStats.csv`, router lanes and request routes in the visualizer |

Presets: `MultiCoreSystem_Grid` (2×2 grid, one LLC), `MultiCoreSystem_Grid2B` (two LLC banks),
`MultiCoreSystem_Grid2BM` (two banks, memory controller on the network), `MultiCoreSystem_Grid2B2M`
(two banks, two memory controllers), `MultiCoreSystem_NetNoC` (the legacy single-switch NoC on the
network engine). The multi-hop network runs directory MSI (the presets) and directory MESI
(`DIRECTORY_MESI` / `MESI_directory_v2`, `DIRECTORY_LLC_MESI` / `MESI_LLC_directory_v2`), including LLC
banks and memory on the network; directory MOESI runs on the bus (`MultiCoreSystem_Directory`) but not
yet on the network, and snooping over the network is not supported.
Real-time arbitration on the network (TDM slots, priority VCs) is not yet available.

## Bus / interconnect arbitration

| Arbiter | Type |
|---|---|
| FCFS | First-come first-serve |
| FR-FCFS | First-ready FCFS |
| RR | Round-robin |
| Weighted-RR | Weighted round-robin |
| Harmonic-RR | Harmonic round-robin |
| TDM | Time-division multiplexing |
| GRROF | Global Round-Robin Oldest-First — coordinated arbitration across all shared resources |

## Cache hierarchy

- **Arbitrary depth:** L1, L2, L3, … with **private or shared** configuration at
  any level
- **Multi-bank** caches
- **Non-blocking** controllers: MSHRs, write-back buffers, refill buffers
- Optimizations: allocate-on-fill, miss forwarding, write-buffer hits
- Blocking and non-blocking controller modes

## Replacement policies

- LRU
- FIFO
- Random
- Extensible to new policies

## Predictable caching / real-time support

- **Set partitioning**
- **Bank partitioning**
- Predictable cache-coherent protocols (see Coherence protocols above)
- Per-request **worst-case latency** tracking to support timing analysis

## Main memory

- **Standalone:** configurable fixed-latency memory model
- **Cycle-accurate DRAM via MCsim integration:**
  - Various DRAM standards (via Ramulator); configurable channels / ranks / banks
  - Per-requestor buffers and criticality-aware scheduling
  - **High-performance controllers:** FCFS, CMDBundle, ORP, MEDUSA, RTMem, ROC,
    RankReorder
  - **Predictable controllers:** REQBundle, MCMC, AMC, DCmc, MAG, PMC, DRAMBulism

## Operating / integration modes

| Mode | CPU source | Memory | Purpose |
|---|---|---|---|
| **Standalone** | trace-based CPU | fixed-latency | fast design-space exploration |
| **Full-system** | gem5 (Arm cores, Linux) | Octopus + MCsim | high-fidelity, real applications |

- **CPU-side integration:** gem5, MacSim
- **Memory-side integration:** MCsim
- **Arm ISA support:** LL/SC (`LDXR`/`STXR`) with per-core exclusivity tracking,
  LSE atomics (`SWP`, `CAS`/`CASP`, `LD`/`ST` arithmetic-logical); boots Linux
- **Adaptive Traffic Profile (ATP)** engine for injecting aggressor (GPU/DPU)
  traffic

## Monitoring and observability

- Per-request latency decomposed into **pipeline stages** (L1 stall, request bus,
  L2 stall, L2 access, response bus, DRAM bus, DRAM), plus a memory-level-
  parallelism-aware **effective** latency
- Per-core **worst-case** latency and total execution-time reports
- Component-level **CSV debug logs** with filters (address, ID, source,
  destination), general-event or message-associated

## Configurability and extensibility

- **Hierarchical configuration** via CSV files and CLI: parameters inherited,
  overridden, and extended modularly
- **Modular, object-oriented** design (inheritance / polymorphism); arbiters,
  replacement policies, memory controllers, and interconnects are **plug-in**
  modules
- Coherence protocols and LLC controllers are **CSV finite-state machines** —
  add or modify without touching C++

---

*Open-source at the project repository. Configuration examples and the
per-axis sweep harness are included so every combination above is reproducible.*
