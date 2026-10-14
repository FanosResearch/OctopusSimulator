# Memory controllers on the network — plan

Status: M1 (§2) done 2026-10-14, see `docs/NoC_Implementation_Report.md`; M0 and M2-M7 proposed. It lifts the "memory controllers on the network" exclusion of
`docs/NoC_Architecture_Spec.md` §1 and the open question 4 of `docs/NoC_MultiHop_Plan.md`. Written
against branch `network` at `220d6522`.

## 1. What exists today

| piece | today |
|---|---|
| system | `MultiCoreSystem_Mesh` gives every LLC bank two interfaces: the network as its lower interface (toward the L1s) and a point-to-point `Bus` as its upper interface (toward memory). `MainMemoryController` hangs off that bus; MCsim cannot be selected on this system |
| banks | `llc_banks(i)` complete LLC controllers; the memory controller answers the bank that asked (`msg.from`), `m_llc_id` is the fallback; `MultiCoreSystem_Grid2B.csv` puts both banks on one arbitrated memory bus off the grid |
| LLC → memory | protocols address memory with the single `m_shared_memory_id` (`LLCMSIDirectory.cpp:86`, `LLCMSIProtocol.cpp:120`, `MESIDirectory.cpp:51`, `CacheController_End2End.cpp:92`); kinds `K_MEM_READ`, `K_MEM_WRITE`; memory answers with `K_FILL` and never acknowledges a write |
| classes | `Classifier.cpp` puts `K_MEM_READ`, `K_MEM_WRITE` and `K_FILL` in RESPONSE: harmless on a private bus, wrong on a shared network (a memory read waits on the memory controller, so it is not always drainable) |
| back-pressure | `MainMemoryController` prints and calls `exit(0)` when its push is refused; `MCsimInterface` likewise |
| accounting | the Logger's Mem-Bus stages come from `MEM_BUS` events; network packets log as `REQ_BUS`/`RESP_BUS`, so they would vanish |
| MCsim | `getMemorySystemInstance` returns `new MultiChannelMemorySystem` (no singleton); its statics are a standard name and a debug probe, so several instances can coexist |

## 2. Read-after-write to memory: the correctness rule first

### 2.1 The rule

A read of a line must observe every earlier write of that line, whether or not the simulator
carries the data. Expected behaviour is **read forwarding**: a read that finds a pending write to
the same line takes its data from that write. Reordering reads and writes of **different** lines is
allowed and is what real controllers do.

Why it matters here: the LLC frees its write-back register when the line becomes invalid, which is
when the write is sent, because memory never acknowledges it. From then on the write in flight is
the only up-to-date copy, and a later miss to the line issues a memory read that can reach memory
first.

### 2.2 Measured on today's bus systems (`OCTOPUS_MEM_HAZARD=1`)

The detector (`header/MemHazard.h`, hooks in `BusInterface.cpp`, `MainMemoryController.cpp`,
`MCsimInterface.cpp`) records, when the LLC sends a memory read, how many writes to that line it has
sent. The memory side reports the read if it is delivered before those writes (NET) or completed
before they were performed (MC). A write the sender pulls back out of its TX buffer and uses as the
fill counts as forwarded. Off, it changes nothing (`run.sh reg` 4/4).

EEMBC, 10 benchmarks per configuration, overtakes summed:

| configuration | NET | MC | forwarded at the sender |
|---|---:|---:|---:|
| snoop MESI, MCsim FR-FCFS, RR memory bus (the local default) | 0 | 0 | 7 747 |
| same, FCFS memory bus | 0 | 0 | 7 747 |
| same, fixed-latency memory | 0 | 0 | 6 929 |
| same, LLC 8 KiB | 0 | 0 | 4 781 |
| snoop MESI, MCsim MEDUSA (write queue) | 0 | 3 196 | 41 |
| directory MSI preset | 884 | 884 | 0 |
| Grid2B (directory, two banks) | 548 | 548 | 0 |

Three mechanisms:

1. **Snoop is safe by construction.** `CacheController_End2End::sendBusRequest` calls `rollback()`
   on its memory-side TX buffer before sending a read; a pending write to the line is removed and
   its data becomes the fill (`K_FILL_ROLLBACK`). That is read forwarding at the sender, and once
   the write has left the TX buffer the single point-to-point link keeps it ahead of any later read.
2. **The directory LLC has no such path.** It pushes the write-back and the read of the same line in
   the same cycle; `FCFSArbiter` keeps the first buffer on a tie and `RRArbiter` behaves the same,
   and the LLC's request TX buffer is congregated before its response TX buffer, so the read wins.
3. **MCsim schedulers with a write queue** (`WriteQueue=1`: MEDUSA, FRFCFS_Batch) hold writes and
   serve reads first with no forwarding in `RequestQueue`. FR-FCFS, the sweep default, has
   `WriteQueue=0`.

On the network the snoop mechanism does not carry over: a write that has left the network
interface cannot be pulled back, and reads and writes travel in different classes (§4).

### 2.3 Fix (built in M1)

- **H1, the LLC keeps every write until memory acknowledges it.** The memory controller emits a write
  acknowledgement (`K_MEM_WACK`, no data, id 0 so no request's latency carries it) when it **accepts**
  the write: from then on its queue orders the line. Acknowledging only when DRAM performs the write
  would let an MCsim write queue that drains at its high watermark withhold acknowledgements while demand
  waits on a full write-back buffer. The LLC controller keeps a table of written lines and their data
  until the acknowledgement, and `sendBusRequest` fills a read of such a line from the table
  (`K_FILL_ROLLBACK`) instead of sending it. It generalises End2End's TX rollback, lives in the
  controller (no FSM change) and holds for any interconnect. The entries count against `pwb_size`.
- **Acknowledgements travel on a sideband** of the memory bus (delivered as emitted, no data slot), like
  the separate write-response channel of AXI or CHI. In a data slot they queued the fills behind them.
- **H2, the memory controller forwards inside its own queue.** `MCsimInterface` answers a read of a line
  whose write MCsim has not performed from that write. `MainMemoryController` serves its queue in arrival
  order, so a write is always performed before a later read of its line.
- **H3, the detector is a test.** `OCTOPUS_MEM_HAZARD=1` reports, `=fatal` exits 2 at the first hazard;
  REG-HAZARD runs it in the regression.
- A request that arrives after its line was chosen as victim waits behind the eviction and refetches,
  now from the pending write. Serving it from the write-back buffer (holding the Replacement for the
  requests queued when it became ready) was considered and not adopted.
- Forwarding costs zero cycles; a configurable latency is issue #13.

**Decided:** on everywhere; bus timings change (finish cycles within ±2.1% on EEMBC except `cacheb01`
under MEDUSA, −11%) and the regression references were regenerated.

## 3. Design decisions (recommended)

1. **A bank's memory side is a second endpoint.** Each LLC bank gets a memory-port id (e.g. bank 10
   has port 110) attached to the same router. Controllers and protocols keep their upper/lower
   interfaces and never learn about the network (spec principle "unchanged endpoints"). The
   alternative, one interface splitting its RX into two views by class, couples the network
   interface to the controller's notion of upper and lower.
2. **Three classes are enough.** `K_MEM_READ` in FORWARD (the LLC consumes a request and emits it;
   the memory controller consumes it and emits a response), `K_MEM_WRITE`, `K_FILL` and `K_MEM_WACK`
   in RESPONSE (on the network the acknowledgement is a header-only packet) (the LLC may write back while consuming a response; fills and acknowledgements are
   always sunk). This passes the existing "emit only a higher class" check. A read in REQUEST would
   fail it. Dedicated memory classes (isolation of memory and coherence traffic) stay a later
   option, worth it only if the real-time results need that isolation. With H1, correctness does
   not depend on any ordering between the classes.
3. **Memory map.** `mem_ids(vi)` + `mem_interleave_shift(i)` on the LLC, mirroring `home_ids` /
   `home_interleave_shift`; `memOf(addr)` replaces the fixed id at the four sites. Same bits as the
   bank map gives bank-to-controller affinity; different bits (e.g. 4 KiB pages) gives all-to-all.
   The presets choose explicitly.
4. **One MCsim per memory controller**, each behind its own endpoint, with the controller-select
   bits removed from the address before DRAM decoding (otherwise each controller sees a sparse
   address space and row-hit rates are distorted).

## 4. Steps

Each step: implement, run its tests (`bash test/network/run.sh <id>`), ASan, a section in
`docs/NoC_Implementation_Report.md`, commit on `network`.

| step | work | validation |
|---|---|---|
| **M0** | spec: lift the exclusion, add a "Memory on the network" section (§2 rule, §3 decisions); test plan: the ids below | review before code |
| **M1** (done) | H1, H2, H3 on the existing bus systems (the bug existed there) | hazard matrix 0 NET / 0 MC in every configuration, 70/70 complete; REG-HAZARD; full suite passes (it exposed a directory L1 gap, fixed in `MSI_directory_v3` / `MESI_directory_v2`) |
| **M2** | one memory controller as a network endpoint: memory-port ids, `memory_on_network(b)` in `MultiCoreSystem_Mesh` (bus path untouched otherwise); memory controller admission instead of `exit(0)` (complete a read only with a response slot free; acknowledgements likewise) | Grid EEMBC 10/10, DRAM stage non-zero, hazard 0; existing presets byte-identical |
| **M3** | classes (§3.2), memory controllers in `checkProtocols`, LLC memory actions annotated | checker passes; negative test (read in REQUEST refused); tiny inject/eject queues on cacheb01 with no deadlock; ASan clean |
| **M4** | accounting: memory packets log as `MEM_BUS` out and back, so the Mem-Bus stages are network time; hop and NoC-wait columns include memory hops; visualizer memory routes and one DRAM lane per controller | event-path tiling 0 failures; `octoviz convert` checks clean; unloaded LLC-to-memory latency equals the hop formula |
| **M5** | several memory controllers: `memOf`, one controller per id, preset with 2 banks and 2 controllers on edge routers | per-controller request counts match the interleave formula; EEMBC 10/10; SPLASH subset completes; hazard 0 |
| **M6** | MCsim on the grid: `main_memory_type` in `MultiCoreSystem_Mesh`, one `MCsimInterface` per controller, address-bit removal | deterministic (md5 over 3 runs); one controller on the grid vs the bus system: same DRAM access count, latency difference explained by the network |
| **M7** | docs: SUPPORTED_CONFIGURATIONS, NoC docs, manual ch. 8/9; regression test with memory on the network | `run.sh reg` 5/5 |

M1 stands on its own: it fixes the bus systems whether or not the rest is done.

## 5. Out of scope

Real-time arbitration at the memory controller and on memory routes: it joins the deferred
TDM_XY / priority-VC work, which will then cover DRAM traffic. Snooping over the network is
unchanged by this plan.
