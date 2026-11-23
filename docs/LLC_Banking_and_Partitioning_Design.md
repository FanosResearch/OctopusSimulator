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
- `MESI_LLC_directory_v2`, `MOESI_LLC_directory_v2`, `MOESI_directory_v2` FSM tables: stale
  PutS / last_PutS and PutM_Data_fromNonOwner are acknowledged; MOESI `EI_a + Fwd_GetS`.

Not ported: admission `restoreLast`, the Classifier, transmit reservations. They are NoC
mechanisms.

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
    // P3:
    int      colour(uint64_t a) const;                 // set-index bits above the page offset
    uint64_t withColour(uint64_t a, int c) const;
    int      colours() const;
private:
    Interleave m_banks;       // this cache's bank group (the banks of THIS level)
    int        m_self_idx;    // this cache's index in m_banks; -1 if not a bank
    Interleave m_home;        // the caches of the NEXT level serving this cache's lines
    int        m_home_fallback;
    int        m_set_bits, m_block_bits;
    Interleave m_sub;         // data-array sub-banks
    int        m_colour_lo = 0, m_colour_bits = 0;     // P3
};
```

The bank group and the home map are separate fields. An L1 has a home map (the LLC banks) and
no bank group. An LLC bank has a bank group (its peers) and an empty home map, so its home is
memory. A single field for both would send an LLC bank's misses to its peers.

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
- Two placing schemes (section 6) that use overlapping address bits are refused.

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
| `ColourPartition` (set partitioning) | `placesAddresses`, `unitOf`/`withUnit` via `AddressMap::colour`, `onFill` | `PagePlacer`; the cache counts violations | P3 |
| `LlcBankPartition` | same, via `AddressMap::home` | `PagePlacer`; the cache counts violations | P4 |
| `SubBankPartition` | same, via `AddressMap::subBank` | `PagePlacer` | later |
| `DramBankPartition` | same, via MCsim's address mapping | `PagePlacer` | later |
| `MshrPartition` | `admit` | the cache | later |

Ownership types: `StaticOwnership` (P1). Later: `OverlappingOwnership` (CAT-style),
`UtilityOwnership` (UCP).

A violation is a fill whose unit is outside the requester's allowed units. It is counted and
reported. `partition[k].strict(i),1` stops the run on the first violation (tests).

### 6.6 PagePlacer: source-side enforcement

Set and bank partitioning cannot be enforced inside the cache: the address fixes the set and the
bank. They are enforced where addresses are chosen, as an OS page allocator does.

`PagePlacer` sits in the trace path (`src/CPU.cpp`), in front of the L1s:

- A page's first access picks, for every scheme with `placesAddresses()`, the next unit the core
  owns (round-robin), and composes them by chaining `withUnit()`.
- One page table serves all cores, keyed by the original page. A shared page maps to one frame
  for every core, so coherence is unaffected. Its units come from the first core that touches it.
- Off by default (`page_placer(i),0`): addresses pass through unchanged.

| Front end | Set/bank partitioning |
|---|---|
| trace-driven Octopus | `PagePlacer`: enforced |
| ATP in gem5 | each master's window chosen from its units, with the same `AddressMap` maths |
| gem5 full system | violations reported; enforcement would need a colouring allocator in the guest kernel |

### 6.7 Explicit requester

`setRequester()` and the hidden `m_requester` are removed. The fill and victim paths take the
requesting core as an argument (`updateLineData(addr, data, core)`, victim selection with
`core`). The two controller call sites (`BaseController.cpp:470`, `CacheController.cpp:301`)
pass `msg->owner`.

### 6.8 Adding a scheme

Example, MSHR partitioning:

1. Write `src/Partitioning/MshrPartition.cpp`: implement `name()`, `units()`, `admit()`;
   register it.
2. Configure `partition[k].scheme(s),mshr` and its `owners`.
3. The contract tests run on it automatically (section 8.2).

No cache, controller, protocol, `PartitionSet` or `PagePlacer` code changes.

## 7. Implementation plan

Each step is one commit with its gate. Steps B (banking) and P (partitioning) share A1.

| Step | Content | Gate |
|---|---|---|
| A1 | `Interleave`, `AddressMap`; `calculate_*` and a new `homeOf` route through it; N = 1 | byte-identical regression; round-trip unit test |
| B1 | home map plumbing: `homeOf`/`isHome` in controller and protocol handler; factory installs it | byte-identical |
| B2 | system builder: `llc_banks`, `llc_controller[k]` on both buses; memory replies to the requester | 1 bank byte-identical; 2-bank directory MSI EEMBC |
| B3 | addressing: every LLC-bound emit names `homeOf(addr)`; LLC filter; assertion | snooping MSI/MESI/MOESI EEMBC at 1/2/4 banks; one supplier per request (trace check) |
| B4 | directory race fixes and v2 FSM tables from `network` 973fe821 | directory MSI/MESI/MOESI EEMBC at 1/2/4 banks, FCFS/RR/TDM |
| B5 | set-index fix active for banks; Logger/octoviz per-bank check | every set of every bank used (occupancy test); banked results re-baselined |
| B6 | optional: broadcast capacity check skips non-home banks | timing difference measured and documented |
| B7 | `sub_banks`: per-sub-bank data-array occupancy | same-vs-different sub-bank latency gap matches the occupancy formula |
| P1 | `PartitionScheme`, `Ownership`, `PartitionSet`, registry; `WayPartition` + `StaticOwnership`; explicit requester; `way_partition` alias | demo acts byte-identical; no-scheme timing within noise |
| P2 | violation counting and report; `strict` | contract tests |
| P3 | `ColourPartition` + `PagePlacer` | per-core sets confined to owned colours; zero violations with the placer on |
| P4 | `LlcBankPartition` | added without touching any file outside its own (modularity check) |
| D1 | docs: `SUPPORTED_CONFIGURATIONS.md`, manual section, presets | |

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
| Placing schemes sharing bits | e.g. banks interleaved at 4 KiB inside colour bits | refused at construction |
| gem5 full system | page allocation is Linux's | violations reported only |
| Merge with `network` | both branches touch `homeOf` and the builders | same names, parameters and semantics; `network` adopts `AddressMap` at merge |
