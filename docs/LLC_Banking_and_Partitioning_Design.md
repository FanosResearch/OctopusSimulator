# LLC Banking and Cache Partitioning: Design

Status: design, not implemented. Branch `llc-banking`, based on `stable` (7afa2659).

This document plans three connected features for the bus-based system on `stable`:

1. A multi-bank LLC, for snooping and directory protocols, without the NoC work.
2. One address-mapping component (`AddressMap`) that every cache uses for set, tag, bank and colour.
3. A modular partitioning framework, so way, set, bank and later schemes are added as plug-ins.

The `network` branch already has LLC banks for directory protocols on the mesh/NoC builder.
This design reuses its names and parameters (`homeOf`, `isHome`, `home_ids`,
`home_interleave_shift`, `mem_ids`, `mem_interleave_shift`, `llc_banks`), so the two branches
merge cleanly.

## 1. Goals and non-goals

Goals:

- An LLC of N banks on the shared bus, for snooping MSI/MESI/MOESI and directory MSI/MESI/MOESI.
- Each line has exactly one home bank, chosen by its address.
- Each bank uses all of its sets. Bank-select bits are not reused as set-index bits.
- Partitioning schemes share one interface. A new scheme is one new class and one registration
  line. No cache, controller or protocol code changes.
- With default parameters (one bank, no partitioning) every output is byte-identical to `stable`.

Non-goals:

- The NoC, memory on the network, and several memory controllers. They stay on `network`.
- Changes to any coherence FSM. Banking is handled by message addressing (section 4).
- Enforcing set/bank partitioning inside gem5 full-system runs. Linux owns the page allocation
  there. Octopus reports violations only (section 6.5).

## 2. Current state on `stable`

| Area | Today | Where |
|---|---|---|
| Set/tag mapping | `calculate_set`, `calculate_tag`, `calculate_address`: plain modulo | `header/CacheDataHandler.h:58-72` |
| Callers of the mapping | `findline`, `findEmptyWay`, COTS write-back | `src/CacheDataHandler.cpp:108,125,232`, `src/CacheDataHandler_COTS.cpp:70` |
| Data-array timing | one `m_ready_cycle` for the whole array | `src/CacheDataHandler.cpp:145-206,269-275` |
| LLC count | one LLC, id `m_shared_memory_id` in every controller | `src/CacheControllers/BaseController.cpp:25` |
| System builder | builds one `llc_controller` on `bus[0]` and `bus[1]` | `src/SystemConfigurations/MultiCoreSystem.cpp:93-99` |
| Memory reply | to a fixed `m_llc_id` | `src/MainMemoryController.cpp:79`, `src/MCsimInterface.cpp:160` |
| Bus requests | `broadcast()` delivers a demand request to every interface | `src/Interconnect/BusController.cpp:34-62` |
| Bus responses | `send()` delivers to `msg.to` only | `src/Interconnect/BusController.cpp:64-95` |
| Way partitioning | `way_partition(s)` masks in the data handler; requester passed by `setRequester()` | `src/CacheDataHandler.cpp:39-60`, `header/CacheDataHandler.h:29-35,101-108` |
| Set/bank partitioning | listed in `SUPPORTED_CONFIGURATIONS.md:83-84`, no code | |

Sites that name the single LLC id and must name the line's home bank instead:

| Site | Message |
|---|---|
| `src/CacheControllers/BaseController.cpp:438` | L1 request / write-back toward the LLC |
| `src/CacheControllers/CacheController.cpp:225-228` | deferred-supply data copy to the LLC |
| `src/CacheControllers/CacheController.cpp:549` | L1 eviction write-back |
| `src/CacheControllers/CacheController_End2End.cpp:92` | End2End write-back |
| `src/CacheControllers/BaseController.cpp:394` | perfect-LLC test of `msg->to[0]` |
| directory L1 protocols (`MSIDirectory`, `MESIDirectory`, `MOESIDirectory`) | requests and PutE/PutM to the home |
| LLC protocol handlers (`LLCMSIProtocol.cpp:120`, `LLCMSIDirectory.cpp:86`) | LLC to memory (stays the single memory id) |

## 3. Component overview

| Component | Responsibility | Changes when |
|---|---|---|
| `Interleave` | spreads lines over N targets; strips and re-inserts the target digit | never |
| `AddressMap` | geometry: set, tag, home bank, sub-bank, colour; pure functions of the address | the hardware layout changes |
| `PartitionScheme` (children) | which resource is split, and where it is enforced | a new resource is partitioned |
| `Ownership` (children) | which core owns which units; static or dynamic | a new allocation algorithm is added |
| `PartitionSet` | holds the configured schemes, forwards each hook to all of them | never |
| `PagePlacer` | source-side placement for every scheme that places addresses | never |
| Caches, controllers, protocols | call `AddressMap` and the `PartitionSet` hooks at fixed points | only in the first step |

One rule shapes the whole design: **where a line lives (its bank and its set) is a function of
its address only, never of the requester.** Lookups, coherence and write-backs depend on it.
The requester may only decide which way a fill uses, or which addresses a core is given.

## 4. Multi-bank LLC

### 4.1 Configuration

The parameters follow `network`:

```
llc_banks(i),2
llc_controller[0].m_id(i),10
llc_controller[1].m_id(i),11
cache_controller[*].home_ids(vi),10,11
cache_controller[*].home_interleave_shift(i),0     # 0: consecutive lines alternate; 6: 4 KiB pages
llc_controller[*].home_ids(vi),10,11               # each bank knows its siblings (set-index fix)
llc_controller[*].home_interleave_shift(i),0
```

- `llc_banks` absent or 1: today's `llc_controller` sub-map is used unchanged.
- Each bank is a complete LLC controller with its own data handler. Its size, ways and latency
  come from its own sub-map. Total capacity is the sum of the banks.
- Bank count need not be a power of two.
- Bus presets list every bank id in `bus[0]`/`bus[1]` interfaces, arbiter candidates and
  `llc_controller.arbiter_candidates_ids`, as the ATP preset does for extra L1s.

### 4.2 Home map

`homeOf(addr)` and `isHome(id)` are added to `BaseController` and `CoherenceProtocolHandler`
with `network`'s semantics:

```
line = addr / block
home = home_ids[(line >> home_interleave_shift) % home_ids.size()]
```

The protocol factory installs the home map in every protocol handler it creates, so no
creation site can forget it (as `network` commit 312e6d65 does). Internally both delegate to
`AddressMap::home()` (section 5).

### 4.3 Addressing: the sender names the bank

Every message an L1 sends toward the LLC carries `msg.to = { homeOf(addr) }`. This replaces
`m_shared_memory_id` at every site in section 2. The sender is the single place that applies
the home map.

On the bus nothing changes:

- `broadcast()` still delivers a demand request to every interface, so every L1 snoops it.
- `send()` still delivers data and write-backs to `msg.to` only, so they reach the home bank.

### 4.4 LLC filter: a bank serves only what names it

A bank that receives a broadcast message on `bus[0]` that does not list its `m_id` in `msg.to`
ignores it before it reaches the protocol. The filter sits in the LLC controller's receive path
(`BaseController`), is protocol-independent, and changes no FSM row.

- Snooping: the non-home bank never sees the GetS/GetM, so it never fetches from memory or
  supplies data. The home bank sees exactly the request stream it sees today for its lines.
- Directory: requests are already point-to-point; the filter never triggers.
- One bank: every LLC-bound message names it; the filter never triggers.

Guard: an L1 message of an LLC-bound kind that names no LLC id is a bug. An assertion reports
the message and stops. Step B3 audits every emit site against this assertion.

### 4.5 Memory side

- Banks share `bus[1]` and the single memory controller.
- `MainMemoryController` and `MCsimInterface` reply to the requesting bank (`msg.from` of the
  request) instead of the fixed `m_llc_id`. The constructor argument stays for the single-bank
  case and for logging.
- LLC protocol handlers keep sending to the memory id. `mem_ids` / `mem_interleave_shift`
  (several memory controllers) remain on `network`.

### 4.6 Bus atomic delivery (optional, separate step)

`broadcast()` holds a demand request until every interface can accept it, including banks that
will ignore it. This is correct but can stall on a full non-home bank. Refinement: skip the
capacity check for LLC interfaces not in `msg.to`. It changes timing, so it is its own step
with its own measurement (B6).

### 4.7 Directory protocols

Banked directory runs need the race fixes `network` made in commit 973fe821, ported without
the NoC parts:

- PutE goes to `homeOf(addr)`; the MOESI L1 identifies home data with `isHome(msg.from)`.
- MOESI: the `MOESI_LLC_directory_v2` and `MOESI_directory_v2` tables (MOESI `EI_a + Fwd_GetS`).
  MESI: `MESI_directory_banked`, an L1 table whose owner can be evicted while it still collects
  acks.
- Stale Puts are acknowledged, not Faults: PutS / last_PutS, and PutM / PutE from a core that is no
  longer the owner, in the LLC states reached by the LLC's own eviction or a refetch. On `stable`
  these cells are in the existing `MSI_LLC_directory` and `MESI_LLC_directory` tables (7fedc5b1);
  `MESI_LLC_directory` is now identical to `network`'s `MESI_LLC_directory_v2`.

Not ported: admission `restoreLast`, the Classifier, transmit reservations. They are NoC
mechanisms.

The B4 gate first ran under FCFS only. Running RR and TDM (test/banking/README.md) exposed three
timing races that banking makes reachable; one LLC never hit them on these traces:

| Commit | Table | Race | Cell |
|---|---|---|---|
| 7fedc5b1 | directory MSI/MESI LLC | a Put loses a race with the LLC's eviction or a new owner | Fault becomes PutAck |
| 722eb8f1 | snooping MSI/MESI/MOESI LLC | a late PutM arrives while the evicted line is refetched (`NM_d`, `NE_d`/`NIorS_d`) | Fault becomes ignore, as in `N` |
| f4d9c2eb | `MOESI_directory_v2` (L1) | an owner still in `IS_d` took a younger `InvM` ahead of a held `Fwd_GetS` | `IS_d + InvM` stalls, as `SM_ad` does |

The last one is MOESI only: with `O` the LLC need not wait after `E + GetS`, so it can evict at once
and invalidate an owner that has not received its data. MESI's LLC waits in `S_d` and stalls
`Replacement` there. Every LLC-bound message of a line still reaches only its home bank (checked by
`check_bank_trace.py` on every run): banking changes the timing, not the protocol.

### 4.8 Logging and visualization

Each bank logs with `Role::LLC` and its own id as the component index. The Logger summary and
`octoviz` occupancy lanes are checked for the assumption of one LLC (step B5).

## 5. AddressMap: one geometry component

### 5.1 Interface

```cpp
// header/AddressMap.h
struct Interleave {                       // spreads lines over N targets
    std::vector<int> ids;                 // target ids
    int shift = 0;                        // granularity: lines alternate every 2^shift lines
    int      index(uint64_t line) const;  // (line >> shift) % N
    int      select(uint64_t line) const; // ids[index(line)]
    uint64_t strip(uint64_t line) const;  // remove the target digit
    uint64_t insert(uint64_t packed, int idx) const;   // exact inverse of strip
};

class AddressMap {
public:
    AddressMap(uint32_t block, uint32_t sets);
    void setBanks(const Interleave &banks, int self_idx); // this cache is one bank of `banks`
    void setSubBanks(int n, int shift);
    void setHome(const Interleave &home, int fallback);   // next-level caches; empty = fallback
    uint64_t line(uint64_t a) const;
    uint64_t set(uint64_t a) const;                    // index in THIS bank
    uint64_t tag(uint64_t a) const;
    uint64_t address(uint64_t tag, uint64_t set) const; // exact inverse
    int      home(uint64_t a) const;                   // which next-level cache serves the line
    bool     isHome(int id) const;
    int      subBank(uint64_t a) const;                // internal data-array bank (timing)
    // page colours (P3): set-index bits above the page offset
    int      colourLo(uint64_t page) const;            // lowest colour bit; -1 = no layout
    int      colours(uint64_t page) const;
    int      colour(uint64_t a, uint64_t page) const;
    uint64_t withColour(uint64_t a, int c, uint64_t page) const;  // keeps page offset and bank
private:
    Interleave m_banks;       // this cache's bank group (the banks of THIS level)
    int        m_self_idx;    // this cache's index in m_banks; -1 if not a bank
    Interleave m_home;        // the caches of the NEXT level serving this cache's lines
    int        m_home_fallback;
    int        m_set_bits, m_block_bits;
    Interleave m_sub;         // data-array sub-banks
};
```

The bank group and the home map are separate fields. An L1 has a home map (the LLC banks) and
no bank group. An LLC bank has a bank group (its peers) and an empty home map, so its home is
memory. A single field for both would send an LLC bank's misses to its peers.

Colours are computed in the cache's own (bank-stripped) line space. A packed set-index bit is a
colour bit when the line bit it comes from is above the page offset. With N banks interleaved
every 2^s lines and P page-offset line bits, the lowest colour bit is P when s >= P (page or
coarser interleave, any N), and max(s, P - log2 N) when s < P and N is a power of two. Line-
interleaved banks of another count scatter a page's lines over non-contiguous set bits and have
no colour layout (`colourLo` = -1); colour partitioning refuses them.

An XOR set hash (folding tag bits into the set index) is not scheduled; it would be a further
`AddressMap` option with its own step.

### 5.2 Decoding

```
line   = addr >> log2(block)
hi     = line >> shift
packed = ((hi / N) << shift) | (line & ((1 << shift) - 1))     # bank digit removed
set    = packed & (sets - 1)              # optional: XOR-fold tag bits into the set
tag    = packed >> log2(sets)
```

`address(tag, set)` reverses each step and re-inserts `m_self_idx`. For a non-bank cache
(L1, single LLC) N = 1 and the formulas reduce to today's `calculate_*`.

This is the set-index fix. On `network` each bank indexes with the unstripped line, so with N
line-interleaved banks every bank uses 1/N of its sets.

### 5.3 Integration

- `CacheDataHandler` owns an `AddressMap`. `calculate_set/tag/address` become one-line
  wrappers, so their callers do not change.
- The handler sets the geometry and sub-banks from its own parameters. The controller installs
  the home map (`home_ids`, `home_interleave_shift`) with `setHome()` right after it creates the
  handler, and the system builder installs the bank group on each LLC bank (`setBanks()`).
  A default map is today's behaviour.
- `homeOf()` and `isHome()` in the controller and in the protocol handler both delegate to the
  handler's map. The protocol handler already holds the data handler, so there is no second copy
  of the home map to keep in step.

### 5.4 Sub-banks (internal data-array banks)

`sub_banks(i)` (default 1) and `sub_bank_shift(i)` split the data array's occupancy model:
`m_ready_cycle` becomes one entry per sub-bank, indexed by `subBank(addr)` in `isReady(addr)`
and in the four places that close the array. Two accesses collide only in the same sub-bank.
This models the LLC bank contention that cache-bank-aware attacks exploit (Bechtel and Yun,
IEEE TC 2024).

### 5.5 Validation at construction

- `address(tag(a), set(a)) == a` (line-aligned) is checked by a unit test over random
  addresses for every configuration in the test matrix.
- A configuration whose colour bits fall inside the page offset is refused.
- Placing schemes (section 6) may share address bits (P5). A core whose owned units of the
  placing schemes have no frame in common (they disagree on a shared bit) is refused, naming the
  core and the schemes.

## 6. Partitioning framework

### 6.1 Two questions, two hierarchies

Every scheme answers:

- **What is split, and where is it enforced?** Ways, colours, banks, MSHR entries. This is the
  `PartitionScheme`.
- **Who owns which units, and does that change?** Static masks, overlapping masks, utility-based
  reassignment. This is the `Ownership`.

They are composed, not inherited, so any ownership type works with any scheme.

```cpp
class Ownership {                                   // who
public:
    virtual ~Ownership() = default;
    virtual uint64_t allowed(int core) const = 0;   // bit mask over the scheme's units
    virtual void onAccess(int core, int unit, bool hit) {}
    virtual void tick(uint64_t cycle) {}
};

class PartitionScheme {                             // what, and where
public:
    virtual ~PartitionScheme() = default;
    virtual const char* name() const = 0;
    virtual int  units() const = 0;

    // cache-side enforcement
    virtual void restrictVictims(int core, uint64_t set, uint32_t &way_mask) {}
    virtual bool admit(int core, uint64_t addr) { return true; }

    // source-side enforcement (addresses chosen outside the cache)
    virtual bool     placesAddresses() const { return false; }
    virtual int      unitOf(uint64_t addr) const { return 0; }
    virtual uint64_t withUnit(uint64_t addr, int unit) const { return addr; }

    // observation
    virtual void onAccess(int core, uint64_t addr, bool hit) {}
    virtual void onFill(int core, uint64_t addr) {}       // violation counting
    virtual void tick(uint64_t cycle) {}
    virtual void report(std::ostream&) const {}
protected:
    std::unique_ptr<Ownership> m_owners;
};
```

Every hook has a no-op default. A scheme implements only the hooks it needs.

### 6.2 PartitionSet

`PartitionSet` holds the schemes configured for one cache and forwards every hook to all of
them. Way masks from `restrictVictims` are ANDed; `admit` answers are ANDed. It holds no policy
of its own. With no schemes it must cost nothing measurable (checked in step P1).

### 6.3 Registry

Schemes and ownership types register themselves, as replacement policies are selected by name
today:

```cpp
REGISTER_PARTITION_SCHEME("way", WayPartition);
REGISTER_OWNERSHIP("static", StaticOwnership);
```

### 6.4 Configuration

```
llc_controller.m_data_handler.partition[0].scheme(s),way
llc_controller.m_data_handler.partition[0].ownership(s),static
llc_controller.m_data_handler.partition[0].owners(s),0:0;1-3:1
llc_controller.m_data_handler.partition[1].scheme(s),colour
llc_controller.m_data_handler.partition[1].owners(s),0:0-127;1-3:128-255
```

- `owners` uses today's `way_partition` syntax, generalised from ways to units: `cores:units`
  with `a-b` ranges; unlisted cores share the unclaimed units.
- The existing `way_partition(s),X` stays as an alias for `partition[0]` = `way` / `static` /
  `X`. Existing demos and presets give identical output.

### 6.5 Schemes

| Scheme | Hooks | Enforced by | Step |
|---|---|---|---|
| `WayPartition` | `restrictVictims` | the cache, on fill and victim choice (today's code, moved) | P1 |
| `ColourPartition` (set partitioning) | `placesAddresses`, `unitOf`/`withUnit` via `AddressMap::colour`, `inPartition` | `PagePlacer`; the cache counts violations | P3 |
| `LlcBankPartition` | same, via the bank group (`AddressMap::banks`); needs page interleave | `PagePlacer`; each bank counts violations | P4 |
| `SubBankPartition` | same, via `AddressMap::subBank` | `PagePlacer` | later |
| `DramBankPartition` | same, via MCsim's address mapping (section 6.9) | `PagePlacer`; `MCsimInterface` counts violations | P6 |
| `MshrPartition` (`mshr`) | `admit`, `allocate`/`release` (section 6.10) | the cache, on admitting a new miss | P7 |
| `PwbPartition` (`pwb`) | same, on a write-back buffer entry (section 6.10) | the cache, on admitting a new miss (requester mode) | P8 |

Ownership types: `StaticOwnership` (P1), `OverlappingOwnership` (`cat`, Intel CAT style,
section 6.11, P9). Later: `UtilityOwnership` (UCP).

A violation is a fill whose unit is outside the requester's allowed units: the scheme's
`inPartition(core, addr, set, way)` returns false. `PartitionSet` counts fills and violations per
scheme and core; at the end of the run they are written to `Partitions.csv` in the report
directory (`Cache,Partition,Scheme,Core,Fills,Violations`) and the total is printed.
`partition[k].strict(i),1` stops the run on the first violation (tests). A cache-enforced scheme
such as `way` cannot violate. A placing scheme violates whenever an address reaches the cache
unplaced (the placer off, or a gem5 full-system run).

An ownership in which a listed core owns no unit is refused at startup (`Ownership::invalid()`):
such a core could never fill. (Before P2 a `way_partition` naming only ways past the cache's
way count ended the run early, with exit code 0.)

### 6.6 PagePlacer: source-side enforcement

Set and bank partitioning cannot be enforced inside the cache: the address fixes the set and the
bank. They are enforced where addresses are chosen, as an OS page allocator does.

`PagePlacer` sits in the trace path (`src/CPU.cpp`), in front of the L1s:

- A page's first access picks the core's next combination of owned units, one per scheme with
  `placesAddresses()`, and composes them by chaining `withUnit()`. Pages go round-robin over all
  the core's combinations in odometer order (the last scheme steps fastest), so a core owning 2
  banks and 2 colours uses all 4 pairs. (Before, each scheme kept its own cursor and all cursors
  advanced together, so only lcm(a, b) of the a x b combinations were used: 2 of 4 here, half the
  core's capacity. With one scheme both are plain round-robin.) Until P5 the schemes' bits had to
  be disjoint: with a shared bit, a later `withUnit()` overwrites an earlier one. The run's
  `PagePlacer:` line reports, per core, how many of its combinations its pages use.
- Joint placement (P5): the frame is chosen for all placing schemes at once. The placer walks
  candidate frames and takes the first free one whose `unitOf()` is a unit the core owns for every
  scheme, keeping the round-robin over the core's units for each scheme where the shared bits allow
  it. Disjoint bits place exactly as before. Overlapping bits are the normal case: under MCsim's
  default FRFCFS mapping the DRAM bank bits (13 to 16) overlap the colour bits of a 1024-set LLC
  (12 to 15) and the LLC bank bits of page interleave (12, 13 at 4 banks), the bank/colour conflict
  that OS allocators such as PALLOC resolve the same way.
- One page table serves all cores, keyed by the original page. A shared page maps to one frame
  for every core, so coherence is unaffected. Its units come from the first core that touches it.
  A fill by another core into such a page is outside that core's units by design; it is counted
  in the `SharedOutside` column of `Partitions.csv`, not as a violation, and never stops a
  strict run.
- Frames are allocated, never computed from the original page: for each tuple of units the
  placer walks candidate frames (`withUnit` applied to page number k = 0, 1, ...) and takes the
  first unused one, so two pages never share a frame.
- Off by default (`page_placer(i),0`, a system parameter): addresses pass through unchanged.
  The placer uses every placing scheme of every cache once (the same scheme on every LLC bank,
  by `signature()`). Two schemes of one name that differ, or different `page_size(i)`, are
  refused at startup; so is a core with no common frame (section 5.5). Before P5, schemes whose
  bits overlap are refused as well.
- Partitions are built when the system is complete (`CacheDataHandler::finalizePartitions`,
  called by both system builders), because colours and banks depend on the bank group the
  builder installs after the caches exist.

| Front end | Set/bank partitioning |
|---|---|
| trace-driven Octopus | `PagePlacer`: enforced |
| ATP in gem5 | each master's window chosen from its units, with the same `AddressMap` maths |
| gem5 full system | violations reported; enforcement would need a colouring allocator in the guest kernel |

### 6.7 Explicit requester

`setRequester()` and the hidden `m_requester` are removed. The fill and victim paths take the
requesting core as an argument: `updateLineData(addr, data, core)`, `writeCacheLine(addr, line,
core)`, `findEmptyWay(addr, core)` and `chooseEvictionWay(set, core)`. The two controller fill
sites (`BaseController::updateCacheLine`, `CacheController::writeCacheLineData`) pass
`msg->owner`. Every other caller passes no core (-1 = unrestricted): the perfect-cache
pre-fill and `initialize()`, which place lines before any core runs.

### 6.8 Adding a scheme

Example, MSHR partitioning:

1. Write `src/Partitioning/MshrPartition.cpp`: implement `name()`, `units()`, `admit()`;
   register it with `REGISTER_PARTITION_SCHEME("mshr", MshrPartition)` inside
   `namespace octopus`, and add the file to `src/Partitioning/CMakeLists.txt`. (That list is
   an OBJECT library linked straight into the binaries; in the static core library the linker
   would drop a file nothing references, and the registration with it.)
2. Configure `partition[k].scheme(s),mshr` and its `owners`.
3. The contract tests run on it automatically (section 8.2).

No cache, controller, protocol, `PartitionSet` or `PagePlacer` code changes.

### 6.9 DramBankPartition

Units are the DRAM banks of the MCsim device (default: the 16 banks of DDR4 8Gb x8, 4 bank
groups x 4 banks; option: the 4 bank groups). Decisions (2026-11-24):

- Placer-side, like colours and LLC banks: the `PagePlacer` gives each core frames whose DRAM
  bank bits are in its banks, so the address alone decides the bank. MCsim's compile-time bank
  privatisation (`CORE4_BANK_PRIVATIZATION` and similar, `bank = requestorID`) is not used: it
  maps one line to different banks for different requesters, and write-backs carry the LLC's id.
- The bank bits come from the `AddressMapping` parameter of the MCsim `.ini` actually loaded
  (`mcsim_scheduler` selects the file; the mapping is a setting of that file, not of the
  scheduler) and the device geometry. At startup `unitOf(withUnit(a, u)) == u` is checked through
  MCsim's own decode. A key to override the mapping from Octopus (`mcsim_address_mapping`) is a
  separate, optional step.
- Violations are counted at `MCsimInterface`: a DRAM access whose bank is outside its core's
  banks. Reads carry the requesting core. A write-back's owner is whoever caused the eviction (the
  LLC, or in snooping the core whose miss evicted the line), not the line's owner, so write-backs
  are attributed to the page's owner in the placer's table (not counted when the placer is off).
- Implementation: MCsim's `AddressMapping::fieldBits` gives each field's position and width;
  `MCsimInterface` records the bank and bank-group fields in `DramMap` with MCsim's own decode.
  `dram_bank` and `dram_bank_group` are two registered names of one class. A scheme with
  `countedAtMemory()` is skipped by the caches' fill counting; `finalizePartitions` hands one copy
  per signature to the `DRAM` `PartitionSet` in `DramMap` (`watch`, keeping its `partition[k]`
  number), whose rows `Partitions.csv` prints after the caches'.
- Refused unless `main_memory_type` is MCsim (fixed-latency memory has no banks).
- Overlap with colour or LLC bank bits is handled by joint placement (P5), which comes first.
- Known MCsim limit: the decode keeps 32 bits of the line address (`unsigned tempA`), so address
  bits 38 and up are dropped. Bank bits are below that; rows of high trace addresses alias.

### 6.10 MSHR and write-back buffer partitions

Ways, colours, LLC banks and DRAM banks split space. MSHRs and the write-back buffer (PWB) are a
second interference channel: they bound how many misses and evictions a cache has in flight, and
one core can hold all of them. Today the LLC turns away a new miss when all `num_mshr` MSHRs are
in use, whoever holds them (`CacheController::canAdmitRequest`; the run's "admit-fail cycles
(MSHR/PWB full)" counter). These two schemes give each core its own entries. They matter only at
a shared cache; on a private L1 every entry already belongs to one core.

State of the code (2026-11-24):

- `PartitionScheme::admit` exists but nothing calls it.
- MSHRs are the controller's `m_pending_requests`, one entry per line address: a new key is a new
  miss, and requests from other cores to the same line join it.
- Every LLC victim goes to the PWB (`moveLine2WB`, from the fill in `updateLineData(addr, data,
  core)`), clean or dirty, and leaves it when its write-back completes (`updateLineBits`). Entries
  are issued oldest first over all cores (`addressOfLinePendingWB`).
- `pwb_size` is enforced only at an L1's CPU intake (`demandAdmissionBlocked`). At the LLC the PWB
  is unbounded; the comment in `canAdmitRequest` that it is "enforced strictly at fill time" is
  out of date (`fillWouldOverflowPwb` has no caller).

Design:

- **Units** are entries: `num_mshr` for `mshr`, `pwb_size` for `pwb`. Owners use the usual
  syntax, so `0:0-3;1-3:4-15` gives core 0 four entries and cores 1-3 twelve shared ones. A scheme
  is refused when its size is unbounded (-1).
- **Entries hold a unit.** A new entry takes a free unit its core owns and keeps it until the
  entry frees. With a disjoint ownership this is a per-core quota; with an overlapping one
  (section 6.11) two cores draw on their common units. The scheme keeps the unit map, so the
  hooks become stateful: `admit(core, addr)` asks whether a unit is free, `allocate(core, addr)`
  takes one, `release(addr)` frees it. `PartitionSet` forwards all three; with no scheme they
  return at once.
- **MSHR** (`mshr`, P7). `canAdmitRequest` calls `admit` for a brand-new miss, after the existing
  global check. The miss is charged to the core that opened it; a request from another core that
  joins it takes no unit. `allocate` runs where a new `m_pending_requests` key is inserted,
  `release` where it is erased. A turned-away miss waits in the queue and counts as an admit-fail
  cycle, as today.
- **PWB** (`pwb`, P8). An entry is charged by `partition[k].pwb_owner(s)`:
  - `requester` (default): the core whose fill evicted the line. The gate is in `canAdmitRequest`:
    a core with no free PWB unit cannot open a new miss. A fill is never refused (a response must
    always proceed, see the back-pressure row in section 9), so a core's entries can exceed its units by
    at most its outstanding misses: entries(c) <= units(c) + mshr(c).
  - `victim`: the core that last placed the line's data in this cache (its fill, or an L1
    write-back into it). This needs a new field, `GenericCacheLine::placed_by`, set on every fill
    and write into the line. The victim is chosen only at fill time, so no admission gate is
    possible: the entry is charged to the victim's core, and an entry with no free unit of that
    core is counted as a violation. This mode is for accounting (who occupies the buffer), not
    isolation: core A's evictions of core B's lines are charged to B.
  - Without a way partition the two modes differ. With one, the victim is in the requester's ways,
    so they agree except on shared lines.
- **Report.** `Partitions.csv` keeps its columns. For these schemes `fills` is entries allocated,
  `violations` is entries charged beyond the core's units (always 0 for `mshr` and for `pwb`
  requester mode, by construction), and the run prints each core's peak entries next to its units.
- **Not covered: drain order.** A quota bounds how many entries a core holds, not how long they
  wait. PWB entries are issued oldest first over all cores and then meet FR-FCFS write batching at
  MCsim. A per-core round-robin issue order (`pwb_drain(s),rr`) is an optional later step (P10),
  with its own test. The LLC's global `pwb_size` bound is a separate decision too: enforcing it
  changes every LLC run, so it is not part of P8.

### 6.11 OverlappingOwnership (Intel CAT style)

`StaticOwnership` already accepts two cores naming the same unit; only the cores it does not list
are limited to the unclaimed units. `cat` adds what Cache Allocation Technology defines on top:

- **Capacity bit masks.** `owners` gives a hex mask per core group, bit u = unit u:
  `0:0x00f;1:0x0f0;2-3:0xff0`. Cores not listed get every unit (CAT's default class of service
  owns the whole cache), unlike `static`.
- **Contiguous masks.** CAT requires the set bits of a mask to be contiguous and non-empty; a mask
  that is not is refused at startup. `partition[k].contiguous(i),0` lifts the rule (Arm MPAM and
  AMD allow arbitrary masks).
- **Reprogramming at run time.** System software changes masks while tasks run.
  `partition[k].schedule(s),<cycle>@<owners>|<cycle>@<owners>` applies a new mask set at each
  cycle, through `Ownership::tick`. This needs `PartitionSet::tick(cycle)` called by the cache
  each cycle (one empty check when no scheme is configured). As on CAT hardware, lines in units a
  core loses stay and still hit; only new fills follow the new masks. A run with no `schedule` is
  identical to `static` with the same masks.
- **Report.** Sharing is the point of overlap, so two columns are appended to `Partitions.csv`
  for every scheme: `shared_fills` (fills into a unit that another core also owns) and
  `evicted_by_other` (this core's lines evicted by another core's fill, using `placed_by` from
  P8). `partition.sh` checks the new header.
- It works with any scheme, as every ownership does: overlapping ways, colours, banks, or MSHR and
  PWB entries (a shared pool next to private entries).

## 7. Implementation plan

Each step is one commit with its gate. Steps B (banking) and P (partitioning) share A1.

| Step | Content | Gate |
|---|---|---|
| A1 | `Interleave`, `AddressMap`; `calculate_*` and a new `homeOf` route through it; N = 1 | byte-identical regression; round-trip unit test |
| B1 | home map plumbing: `homeOf`/`isHome` in controller and protocol handler; factory installs it | byte-identical |
| B2 | system builder: `llc_banks`, `llc_controller[k]` on both buses; memory replies to the requester | 1 bank byte-identical; 2-bank directory MSI EEMBC |
| B3 | addressing: every LLC-bound emit names `homeOf(addr)`; LLC filter; assertion | snooping MSI/MESI/MOESI EEMBC at 1/2/4 banks; one supplier per request (trace check) |
| B4 | directory race fixes and v2 FSM tables from `network` 973fe821 (and the RR/TDM fixes of section 4.7) | directory MSI/MESI/MOESI EEMBC at 1/2/4 banks, FCFS/RR/TDM |
| B5 | set-index fix active for banks; Logger/octoviz per-bank check | every set of every bank used (occupancy test); banked results re-baselined |
| B6 | optional: broadcast capacity check skips non-home banks | timing difference measured and documented |
| B7 | `sub_banks`: per-sub-bank data-array occupancy | same-vs-different sub-bank latency gap matches the occupancy formula |
| P1 | `PartitionScheme`, `Ownership`, `PartitionSet`, registry; `WayPartition` + `StaticOwnership`; explicit requester; `way_partition` alias | demo acts byte-identical; no-scheme timing within noise |
| P2 | violation counting and report; `strict` | contract tests |
| P3 | `ColourPartition` + `PagePlacer` | per-core sets confined to owned colours; zero violations with the placer on |
| P4 | `LlcBankPartition` | added without touching any file outside its own (modularity check) |
| D1 | docs: `SUPPORTED_CONFIGURATIONS.md`, manual section, presets | |
| P5 | joint placement in `PagePlacer` (section 6.6): one frame for all placing schemes; refuse only a core with no common frame | `partition.sh` unchanged; unit test: two placing schemes on overlapping bits place every page inside both, an infeasible ownership is refused with a message (colours and LLC banks are disjoint by construction, so the first real overlap is P6) |
| P6 | `DramBankPartition` (section 6.9) | DRAM-bank confinement per core on private traces, zero violations with the placer on; colour + DRAM bank under the default FRFCFS mapping; a changed `AddressMapping` followed; refused without MCsim; regression byte-identical |
| P7 | stateful `admit`/`allocate`/`release` in `PartitionScheme` and `PartitionSet`, called from `canAdmitRequest` and the `m_pending_requests` insert/erase; `MshrPartition` (`mshr`) in a new file (section 6.10) | regression and `banked.sh` byte-identical with no scheme; with `mshr`, every core's peak entries <= its units; a streaming core no longer delays a victim core's misses on MSHRs (victim core's worst-case latency drops); refused when `num_mshr` is -1 |
| P8 | `PwbPartition` (`pwb`) in the same file; `pwb_owner(s)` = `requester` (default) or `victim`; `GenericCacheLine::placed_by` (section 6.10) | byte-identical with no scheme; requester mode: entries(c) <= units(c) + mshr(c) on every run; victim mode: charges match a trace count of evicted lines by placing core; refused when `pwb_size` is -1 |
| P9 | `OverlappingOwnership` (`cat`): hex masks, contiguity check, `schedule`, `PartitionSet::tick`; `shared_fills` and `evicted_by_other` columns (section 6.11) | the contract tests pass with `cat`; `cat` with disjoint masks gives the same timing as `static`; a non-contiguous mask is refused; a scheduled mask change takes effect at its cycle (fills before and after) |
| P10 | optional: per-core round-robin PWB issue order (`pwb_drain(s),rr`) | default order byte-identical; with `rr`, no core's write-back waits behind more than one entry of each other core in the PWB |

B1 to B5 give stable a multi-bank LLC. P1 can start after A1, independent of B.

## 8. Testing

### 8.1 Regression

- Byte identity: every step until B5 keeps one-bank, no-partition outputs (LatencyReport,
  Summary, trace) identical to `stable`.
- EEMBC matrix per protocol family at 1, 2 and 4 banks, FCFS/RR/TDM, line and page interleave.
- SPLASH subset for snooping MOESI at 2 banks.

### 8.2 Unit and contract tests

- `AddressMap`: round trip; `home()` agrees with `network`'s `homeOf` formula; colour bits above
  the page offset.
- Every registered scheme, automatically:
  - with no owners configured it changes nothing;
  - a core never receives units outside its mask;
  - `withUnit(a, unitOf(a)) == a` and `unitOf(withUnit(a, u)) == u` for placing schemes.

### 8.3 Banking properties

- Every LLC-bound request names exactly one bank, and it is `homeOf(addr)`.
- No request receives data from more than one LLC bank.
- A bank's trace contains only lines it is home for.

## 9. Risks and open questions

| Item | Risk | Handling |
|---|---|---|
| Bus arbiter with several LLC endpoints | owner-slot election and response channel may assume one LLC | read-through in B2; EEMBC under RR/TDM in B3/B4 |
| Unaddressed LLC-bound messages | a missed emit site would be dropped by the filter | assertion in 4.4; audit in B3 |
| Snooping write-back ordering per bank | a line's write-back and a later request now meet at one bank, as today; no cross-bank ordering exists | covered by the single-supplier and EEMBC checks |
| Broadcast capacity stall | a full non-home bank holds demand requests | correct; B6 removes it if measured as significant |
| Hook overhead | hooks run on every fill | empty `PartitionSet` fast path; P1 timing gate |
| Placing schemes sharing bits | e.g. banks interleaved at 4 KiB inside colour bits, DRAM bank bits inside colour bits | joint placement (P5); a core with no common frame is refused |
| gem5 full system | page allocation is Linux's | violations reported only |
| MSHR/PWB gates and back-pressure | a gate that refuses a response or a write-back deadlocks the system (the request/response shared-queue deadlock fixed in eb9744a6) | gates act only on brand-new demand misses in `canAdmitRequest`; fills, snoops and write-backs always proceed; RR/TDM `banked.sh` runs with the schemes on |
| PWB quota overshoot | requester mode allows entries(c) up to units(c) + mshr(c) | stated bound; checked in P8; an `mshr` partition next to `pwb` caps it |
| Merge with `network` | both branches touch `homeOf` and the builders | same names, parameters and semantics; `network` adopts `AddressMap` at merge |
