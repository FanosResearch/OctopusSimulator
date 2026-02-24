# Changelog — OctopusSimulator Bug Fixes (Feb 2026)

## Bug 1: Rollback response discarded by destination filter (DEADLOCK)

**Severity:** Critical — causes permanent deadlock
**File:** `src/CacheController_End2End.cpp`

### Root Cause

When an LLC bank's `sendBusRequest` finds matching writeback data already in the TX buffer, it performs a "rollback" — copies the data directly to RX instead of sending a request to DRAM. However, the message's `msg.to` field was still set to `[100]` (the DRAM ID), as originally populated by the `GetData` FSM action.

The destination filter in `CacheController::addRequests2ProcessingQueue` (lines 248–264) checks whether `m_core_id` (e.g., 51 for LLC bank 51) appears in `msg.to`. Since `[100] != 51`, the rollback response is **silently discarded**. The LLC bank remains stuck in the `NM_d` state forever, which cascades to permanently deadlock the requesting L1 core (stuck in `IM_d`).

The Point2Point bus uses **unicast** delivery (`BusController::send()`), so even though the message is pushed directly to the RX buffer (bypassing the bus), the destination filter still applies.

### Fix

Before calling `pushMessage2RX`, clear `msg.to` and set it to `this->m_core_id`:

```cpp
msg->to.clear();
msg->to.push_back((uint16_t)this->m_core_id);
m_upper_interface->pushMessage2RX(*msg, MessageType::DATA_RESPONSE);
```

### Symptom

- radiosity deadlocked at ~18M cycles (RROF) and ~4.4M cycles (FRFCFS)
- radix and raytrace also deadlocked
- After fix: radiosity completes at 1,061M cycles (RROF), 551M cycles (FRFCFS)

---

## Bug 2: FRFCFS dirty writeback leak in RequestorsQueues (UNBOUNDED QUEUE GROWTH)

**Severity:** High — causes progressive slowdown, eventual halt
**Files:** `src/MCsimInterface.cpp`, `header/MCsimInterface.h`

### Root Cause

When an LLC bank evicts a dirty cache line, `checkReplacements` adds the writeback request to `RequestorsQueues` (the global singleton for end-to-end latency tracking). Each request has a removal count (phase) — it must be decremented to 0 for the request to be fully removed.

For **reads**, the cache controller removes the request when the response returns (via `writeCacheLineData` → `removePendingAndRespond`). For **writes** (dirty writebacks), removal depends on the DRAM scheduler:

- **RROF:** `CommandScheduler_RROF` explicitly calls `requestorsQueues->removeRequest()` after issuing the WR CAS command (line 136 of `CommandScheduler_RROF.h`).
- **FRFCFS:** Has **no** equivalent `removeRequest` call. The internal DRAM queue (`requestQueue[0]->removeRequest()`) removes from the DRAM's own tracking, but NOT from the global `RequestorsQueues`.

As a result, every dirty writeback in FRFCFS mode leaked in `RequestorsQueues`. The queue grew unboundedly (observed up to 3.7M+ entries), making every O(n) scan in `isRequestExist`, `getRequest`, and `removeRequest` progressively slower.

### Fix

Track pending writes in a new `m_pending_writes` vector in `MCsimInterface`. When DRAM completes a write and calls `write_callback`, match the completed address against `m_pending_writes` and call `removeRequest` to clean up from `RequestorsQueues`:

```cpp
// In processLogic(), when a write is accepted by DRAM:
m_pending_writes.push_back(ready_msg);

// In write_callback(), when DRAM completes the write:
for (int i = 0; i < (int)m_pending_writes.size(); i++)
{
    if (m_pending_writes[i].addr == address)
    {
        if (m_requestors_queues->isRequestExist(...) >= 0)
            m_requestors_queues->removeRequest(...);
        m_pending_writes.erase(m_pending_writes.begin() + i);
        return;
    }
}
```

This works for both RROF (where CommandScheduler_RROF may have already removed it — hence the guard) and FRFCFS (where this is the only removal path).

### Symptom

- FRFCFS radiosity: RequestorsQueues size grew from 0 to 3.7M+ entries
- Wall time ballooned from ~40 min to 250+ min
- After fix: queue stabilizes at ~140 entries (normal pipeline depth), completes in ~40 min

---

## Bug 3: Double removeRequest stderr noise for RROF writes (COSMETIC)

**Severity:** Low — cosmetic, no functional impact
**Files:** `src/MCsimInterface.cpp`, `src/MCsim/src/RequestorsQueues.h`, `header/MCsim/MCsim.h`

### Root Cause

After the Bug 2 fix, both `CommandScheduler_RROF` (DRAM side) and `MCsimInterface::write_callback` (cache side) call `removeRequest` for the same write. The first call succeeds and removes the request. The second call finds the request already gone and prints to stderr:

```
Error RequestorsQueues: removeRequest: Request is not exist <reqID> <coreID>
```

This happens thousands of times per RROF run, cluttering stderr.

### Fix

Guard the `removeRequest` call with an `isRequestExist()` check:

```cpp
if (m_requestors_queues->isRequestExist(owner, msg_id, NULL, NULL) >= 0)
    m_requestors_queues->removeRequest(owner, msg_id);
```

This required making `isRequestExist` public (was private) in both:
- `src/MCsim/src/RequestorsQueues.h` — the implementation header
- `header/MCsim/MCsim.h` — the forward-declaration interface header

---

## Bug 4: RequestorsQueues O(n) linear scans (PERFORMANCE)

**Severity:** Medium — causes O(n^2) total runtime for long simulations
**Files:** `src/MCsim/src/RequestorsQueues.h`, `src/MCsim/src/RequestorsQueues.cpp`

### Root Cause

`unifyQueue` was a `vector<pair<uint, pair<uint,uint>>>` scanned linearly in `removeRequest` to find and erase entries by request ID. With large queues (hundreds of thousands of entries for long benchmarks), each removal was O(n), making total cost O(n^2).

Additionally, `isRequestExist` scanned all cores' queues linearly to check for request existence.

### Fix

1. **Changed `unifyQueue` from `vector` to `list`** — O(1) erase by iterator instead of O(n) shift
2. **Added `unifyIterMap`** (`unordered_map<reqID, list::iterator>`) — O(1) lookup of where a request lives in the list, enabling O(1) removal
3. **Added `requestIndex`** (`unordered_map<reqID, pair<coreID, isYounger>>`) — O(1) existence check and core lookup in `isRequestExist`, avoiding full scan of all cores' queues
4. **Fixed `clearRequests()`** to also clear the new `unifyIterMap` and `requestIndex` data structures
5. **Fixed `WCL_logger` handling in `removeRequest`** — the latency logger for a completed request was only written when a younger request existed to promote. Now it always logs the completed request, and the promotion timestamp is set unconditionally when a younger request is promoted.

### Complexity Summary

| Operation | Before | After |
|-----------|--------|-------|
| `removeRequest` unifyQueue cleanup | O(n) linear scan | O(1) hash + iterator erase |
| `isRequestExist` | O(all_cores x queue_depth) | O(1) hash lookup + O(per_core_depth) |
| `add2UnifiedQueue` | O(1) vector push | O(1) list push + hash insert |

---

## Script Enhancement: run_benchmarks_parallel.sh

**File:** `run_benchmarks_parallel.sh`

### Changes

1. **Dual-scheduler support** — Runs both RROF (`tc_FR_4E_RROF.xml`) and FRFCFS (`tc_FR_4E_FRFCFS.xml`) for each benchmark, with separate results directories (`{benchmark}_RROF/`, `{benchmark}_FRFCFS/`)
2. **Timing instrumentation** — Uses `/usr/bin/time -v` to capture wall time and peak RSS for each run
3. **Timing summary CSV** — Generates `results/timing_summary.csv` with benchmark, scheduler, wall time, RSS, exit code, finish cycle, and average latency. Uses `flock` for thread-safe writes from parallel jobs
4. **Fixed wall time parsing** — Original regex `:.*:.*:` required 3 colons, but `h:mm:ss` format only has 2. Runs exceeding 1 hour had their wall time recorded as just the minutes value (e.g., `1:04:13` parsed as 64 seconds instead of 3853 seconds). Fixed by counting colons with `tr -cd ':' | wc -c`

---

## Diagnostic Infrastructure

**Files:** `src/MCsimInterface.cpp`, `header/MCsimInterface.h`

Added `dumpDiagnostics()` method to `MCsimInterface`, triggered by `CacheController::s_global_dump_triggered`. Dumps:
- Processing queue size
- Pending DRAM reads (msg_id, address, owner, target LLC bank)
- Output buffer contents
- Lower interface (Point2Point bus) FIFO sizes
- Total read/write completion counts

This was used during deadlock investigation and remains useful for future debugging.

---

## Verification

All 13 SPLASH-2 benchmarks completed successfully with both RROF and FRFCFS schedulers (26 total runs, 0 failures). Previously deadlocking benchmarks (radiosity, radix, raytrace) now complete correctly. Memory request counts match between RROF and FRFCFS for identical benchmarks, confirming functional correctness.
