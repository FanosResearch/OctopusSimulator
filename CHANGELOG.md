# Changelog

## gem5-grrof-optimizations — Port grrof optimizations, deque FIFO buffers, and standalone CPU mode

### 1. Standalone CPU Mode (`setup2`)

**Files:** `src/MCoreSimProject.cc`, `header/MCoreSimProject.h`

Added `setup2()` function for standalone (non-gem5) simulation. The existing `setup1()` creates `ExternalCPU` instances that wait for gem5 to inject requests — unsuitable for standalone benchmarking. `setup2()` creates trace-driven `CPU` instances that read `.trc.shared` files from `BMsPath`.

The constructor routes automatically:
- `BMsPath` is set → `setup2()` (standalone mode with trace-driven CPUs)
- `BMsPath` is empty → `setup1()` (gem5 mode with ExternalCPUs)

Also fixed the FSM protocol path from a hardcoded `/workspaces/OctopusSimulator/` to a relative path derived from `getcwd()`.

---

### 2. Silent Eviction Ghost Request Cleanup

**Files:** `src/CacheController_End2End.cpp`, `header/CacheController_End2End.h`

When an LLC cache line is silently evicted (clean eviction with no writeback reaching DRAM), the corresponding entry in `RequestorsQueues` was never removed. This caused unbounded queue growth and incorrect RROF scheduling decisions.

Fix: In the `UPDATE_CACHE_LINE` action handler, detect silent evictions (line transitioning to Invalid with no data payload and self-owned) and immediately clean up the ghost entry from `RequestorsQueues`, `m_wb_cores`, and `m_wb_address`.

Added `m_pending_eviction_owner` field to correctly attribute evictions to the requesting core, replacing the less accurate `m_owner_of_latest_data` in `checkReplacements`.

---

### 3. Dirty/Clean Writeback Split

**Files:** `src/CacheController_End2End.cpp`, `header/GenericCacheLine.h`, `src/GenericCacheLine.cpp`, `src/Protocols/LLCMSIProtocol.cpp`

Previously, all LLC writebacks were sent to DRAM regardless of dirty/clean status. For clean lines, DRAM never issues `write_callback`, so the `RequestorsQueues` entry was never removed — another memory leak path.

Fix:
- Added `dirty` flag, `setDirty()`, and `isDirty()` to `GenericCacheLine`
- `LLCMSIProtocol` marks lines dirty when entering Modified state (`SetOwner` action)
- `performWriteBack` now checks `isDirty()`:
  - **Dirty lines**: sent to DRAM; `RequestorsQueues` removal deferred to `MCsimInterface::write_callback`
  - **Clean lines**: not sent to DRAM; `RequestorsQueues` entry removed immediately

---

### 4. MCsimInterface Write Callback Fix

**Files:** `src/MCsimInterface.cpp`, `header/MCsimInterface.h`

Added `m_pending_writes` tracking for write messages sent to DRAM. The `write_callback` now removes completed writes from `RequestorsQueues`. This is the primary removal path for FRFCFS dirty writebacks (RROF's `CommandScheduler_RROF` also removes after WR CAS, so a guard prevents double-removal).

Also fixed hardcoded `num_cores = 90` to use `projectXml.GetNumPrivCore()`.

---

### 5. RequestorsQueues O(n^2) to O(1) Optimizations

**Files:** `MCsim/src/RequestorsQueues.cpp`, `MCsim/src/RequestorsQueues.h`

Replaced linear scans with hashmap-based lookups:

| Operation | Before | After | Data Structure |
|-----------|--------|-------|----------------|
| `isRequestExist` | O(n) scan all queues | O(1) lookup | `requestIndex` — `unordered_map<reqID, (coreID, isYounger)>` |
| `getRequest` orig_coreID | O(n) scan `unifyQueue` | O(1) lookup | `unifyMap` — `unordered_map<reqID, (addr, coreID)>` |
| `removeRequest` from `unifyQueue` | O(n) scan + O(n) erase | O(1) lookup + O(1) erase | `unifyIterMap` — `unordered_map<reqID, list::iterator>` |
| `ifOldest_promote` address lookup | O(n) scan `unifyQueue` | O(1) lookup | `unifyMap` reuse |

Changed `unifyQueue` from `vector` to `list` for O(1) middle-element removal.

Made `isRequestExist` public (needed by `MCsimInterface` for the write callback guard).

Added bounds checking in `getRequest` for empty oldest queues and out-of-bounds slot access.

---

### 6. Vector to Deque FIFO Optimization

**Files:** 22 header/source files across Arbiters, Bus, CacheController, Interconnect, MCsimInterface, ExternalMem, FRFCFS_Buffer

All message FIFO buffers changed from `std::vector<Message>` to `std::deque<Message>`. The original code used `vector::erase(begin())` for FIFO pop operations, which is O(n) due to element shifting. `deque::pop_front()` is O(1).

Affected components:
- `BusInterface` / `CPUInterface` — TX/RX buffers
- `CacheController` — processing queue output
- `Arbiter`, `FCFSArbiter`, `RRArbiter`, `RROFArbiter`, `TDMArbiter` — request buffers
- `Bus` — message buffers
- `ExternalMem`, `FRFCFS_Buffer` — output buffers
- `MCsimInterface` — output buffer (also changed `erase(begin())` to `pop_front()`)
- Various interconnect controllers

---

### 7. Stale Writeback Tracking Cleanup

**File:** `src/CacheController_End2End.cpp`

In `checkReplacements`, when a new writeback is tracked for an address that already has a pending writeback (`m_wb_address`), the old entry is now properly cleaned up from `RequestorsQueues` and `m_wb_cores` before adding the new one. Previously, the old entry would leak.

---

### 8. Bus Rollback Destination Fix

**File:** `src/CacheController_End2End.cpp`

In `sendBusRequest`, when a rollback returns cached data, the response message's `to` field is now set to the LLC bank's own ID. Previously it retained the DRAM ID (100), causing `addRequests2ProcessingQueue` to silently discard the response due to destination mismatch, deadlocking the requesting core.

---

### 9. Cached Pointer Optimization

**Files:** `src/CacheController_End2End.cpp`, `src/CacheController.cpp`, `header/CacheController.h`

Replaced repeated `AddrMapping::getAddrMapping()` and `RequestorsQueues::getReqQObj()->getRequestorsQueues()` singleton accessor calls with cached pointers (`m_addr_cached`, `m_rq_cached`). Reduces function call overhead in hot paths.

---

### 10. RROFArbiter Improvements

**Files:** `src/Arbiters/RROFArbiter.cpp`, `header/Arbiters/RROFArbiter.h`

- Changed message buffer from `vector` to `deque`
- Used cached `RequestorsQueues` pointer

---

### 11. New XML Configurations

**Files:** `test/arm_challenge/tc_FR_4C_1B_rrof.xml`, `test/arm_challenge/tc_FR_4C_1B_frfcfs.xml`

New configurations for 4-core, single LLC bank benchmarking:
- `logFileGenEnable="1"` — enables LatencyReport CSV generation
- `MCSIM_EN="1"` — uses MCsim DRAM simulator
- RROF variant: `globalQueues_en="1"`, `memArb="RROF"`
- FRFCFS variant: `globalQueues_en="0"`, `memArb="FRFCFS"`

---

### 12. XML Configuration Updates

**Files:** `tc_FR_1C_1B.xml`, `tc_FR_4C_1B.xml`, `tc_FR_4C_8B.xml`

- Enabled `logFileGenEnable="1"` and `MCSIM_EN="1"` in existing configs
- Renamed 10-core configs to 4-core variants (`tc_FR_10C_8B_bnpart*` → `tc_FR_4C_8B_bnpart*`)

---

### 13. Benchmark Runner Script

**File:** `run_benchmarks_parallel.sh` (new)

Script to run all SPLASH-2 benchmarks in parallel for both RROF and FRFCFS schedulers. Features:
- Runs all 13 SPLASH benchmarks x 2 schedulers = 26 parallel jobs
- Per-benchmark timing via `/usr/bin/time -v`
- Results organized in `results/{benchmark}_{scheduler}/` directories
- Generates `results/timing_summary.csv` with wall time, RSS, exit code, finish cycle, and average latency

---

### 14. MCsim Library Build Fix

**File:** `MCsim/src/Makefile`

Changed from `-std=c++11` to `-std=c++17` to support `unordered_map` and other C++17 features used in the RequestorsQueues optimization.

---

### 15. Miscellaneous

- Removed redundant `cout` error message in `RequestorsQueues::removeRequest` (kept `DEBUG` macro)
- Added `callActionFunction` signature change to `const ControllerAction &` (pass by const reference instead of by value)
- `LLCMSIProtocol`: minor restructuring of `SetOwner` case for readability
- `ClockManager`: deque include for consistency
