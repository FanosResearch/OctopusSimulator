# Commit plan for `gem5_integration` (base 2bccdcfe)

Purpose: split the uncommitted work in this tree into reviewable commits, in an
order where every commit builds and the standalone lab presets stay
bit-identical to upstream until the commit that says otherwise. Each entry
gives the commit message to use, the files and hunks it takes, and a
**Background** section for reviewers: the concrete sequence of events that
exposed the problem the commit fixes, so the change can be judged against it.

The last section lists the improvements still open. Build and run commands
are in the top-level `README.md`. The former `INTEGRATION_WORKLOG.md` and
`LL_SC_WFE_INTEGRATION_PLAN.md` (day-by-day history and measurements) are
kept unchanged under `gem5/attic/`, ignored by git.

Conventions used below:

- "hunk" means a `git add -p` hunk; several files carry hunks from more than
  one commit (`CacheController.{h,cpp}`, `BaseController.{h,cpp}`,
  `FRFCFS_Buffer.h`, `CacheDataHandler.{h,cpp}`, `MultiCoreSystem.cpp`,
  `MultiCoreSystem_gem5.csv`). Hunks are named by the function or member they
  touch.
- L1 protocol table: `Protocols_FSM/MSI_splitBus_snooping.csv`; LLC table:
  `Protocols_FSM/MSI_LLC.csv`. "ctl N" is a controller id as printed by the
  trace: L1 data caches are 4..7, the LLC is 10 in the gem5 preset.
- Regression = `/tmp/oct_regress.sh`: per-request latency reports of the
  `MultiCoreSystem_Directory` and `MultiCoreSystem_Snoop` presets diffed
  against a build of untouched upstream 2bccdcfe. "identical" means byte-equal.
- llsc = the LL/SC + WFE spinlock test on gem5 O3 x4 restored from the
  `test_start` checkpoint; tick counts are `simTicks` of the ROI.
- Every item in a commit message carries a tag saying what kind of change it
  is, so a reviewer knows what can move a result:
  `[fix]` corrects wrong behaviour (crash, hang, wrong order); timing may move
  as a consequence. `[model]` changes the timing model on purpose; results
  move and the message says by how much. `[api]` new interface for an
  embedder; nothing changes for existing presets. `[check]` invariant check:
  changes only what happens on a failure (abort with a message instead of a
  crash, or a counted continue); inert on a correct run. `[debug]`
  observability only, off by default, no behaviour change. `[config]`,
  `[build]`, `[docs]` as named.

## Step 0: what not to commit

Already handled by `.gitignore` additions in this tree: harness binaries under
`gem5/harness/` (sources and `build.sh` stay), `gem5/configs/__pycache__/`,
`gem5/attic/` (stale copies of the pre-port bridge, and the two superseded
documents named in the preamble) and `build_asan/` (the AddressSanitizer build used once to rule out memory
corruption; it found only upstream's `delete[]`/`new` mismatch in
`modifyData`, which is not touched here).
Check with `git status --short`; nothing under those paths should appear.

---

## Commit 1: Linux build, simulated-time API, configurable MCsim clock

```
build: Linux portability, clock reporting, configurable MCsim period

[build] ClockManager.cpp included <io.h> and used _isatty(), which do not
exist on Linux. Use <unistd.h> / isatty().

[api] ClockManager::getCurrentTime(), getMinPeriod(), getStepGranularity()
(gcd of all registered periods) and reportClocks(). An embedder that
drives the simulator from an external event loop paces it by simulated
time, not by step count: clkStep() advances to the next clocked object,
and how far that is depends on the finest registered period, which only
the manager knows.

[check] init() aborts with a message if any clocked object is still at
period 0. Every object registers at 0 and sets its period in its own
constructor; a forgotten one silently makes clkStep() spin.

[fix] MCsimInterface registered itself at 1 ns regardless of the memory
model's real clock: with a 100 ns core cycle that is 100 update() calls
per core cycle and a 1 ns step granularity for the whole system. The
period is a constructor argument; MultiCoreSystem reads
`mcsim_clk_period` (default 1, so presets that do not set it are
unchanged).

Effect on results: none for existing presets. Validation: default preset
standalone output unchanged; regression identical.
```

Files: `header/ClockManager.h`, `src/ClockManager.cpp`, `header/MCsimInterface.h`,
`src/MCsimInterface.cpp`, `src/SystemConfigurations/MultiCoreSystem.cpp`
(only the `mcsim_clk_period` hunk).

**Background.** Found when wiring the gem5 bridge: the bridge steps Octopus
until `now() >= gem5 time`, and with the 1 ns MCsim clock the step
granularity was 1 ns, so one gem5 cycle needed 100 steps and the DRAM model
was being updated 100x too often relative to the cores. `reportClocks()`
prints the registered periods at init precisely so this is visible
(`ClockManager: registered clock periods (ns): 50x10 100x18`).

---

## Commit 2: embedding API, RequestType header, config parser bound

```
api: CacheSim embedding API, RequestType header, bounded config parser

[api] CacheSim.h no longer includes the system-configuration headers, so
an embedder needs neither the whole header tree nor the
CONFIGURATION_PATH macro. CacheSim(system_name, cl_params, print_config,
config_name) lets a caller select a CSV other than the class's own
(MultiCoreSystem builds MultiCoreSystem_gem5.csv this way); now(),
stepGranularity() and minPeriod() expose the clock.

[api] RequestType moves from CPU.h into RequestType.h so ExternalCPU.h
does not pull in the trace-driven CPU.

[fix] Configurable::parseLine read string values into a 100-byte stack
buffer with no length check; an override value longer than that
overflowed it ("stack smashing detected" at start-up). The buffer is
MAX_LINE_SIZE and parseCLparam rejects longer values with a message.

Effect on results: none. Validation: regression identical.
```

Files: `header/CacheSim.h`, `src/CacheSim.cpp`, `header/RequestType.h` (new),
`header/CPU.h`, `header/Configurable.h`, `src/Configurable.cpp`,
`header/SystemConfigurations/MultiCoreSystem.h` and `.cpp` (the `config_name`
constructor hunk).

**Background.** The bridge passes `workload_path(s)=<gem5 outdir>` as an
override so Octopus writes its logs next to gem5's. gem5 output directories
are long absolute paths; the first one longer than 100 bytes smashed the
stack inside `parseLine` before a single cycle ran.

---

## Commit 3: ExternalCPU port with credit-based admission

```
cpu: ExternalCPU port, controller hooks, credit-based admission

[api] ExternalCPU is a CPU model driven by an embedder instead of a trace:
addRequest(addr, type, data, size) returns a msg_id; completion, commit
and invalidate callbacks report back; attachCache() binds it to its L1.
MultiCoreSystem builds it for `cpu_type(s)=ExternalCPU`.

[model] Admission works like a real L1 port and like gem5's classic cache:
the port is blocked, not the request capped. ExternalCPU::isBlocked(n)
is true while the embedder's outstanding count has reached the L1's
processing_queue_size, while the MSHRs are full, or while the write-back
buffer is full (BaseController::demandAdmissionBlocked and the
CacheController override). The preset sets processing_queue_size to
64 = the O3 core's LQ 32 + SQ 32, because the queue must hold everything
the core can have issued: the younger loads and stores behind a miss to
the same line wait in it as per-line followers (hardware parks them in
the LQ or as MSHR targets), and they cannot be handed back, since O3
issues each request once and keeps its LQ entry only for ordering and
squash. A squashed load keeps its credit until its response returns.

[api] Controller hooks. commit() is called at the serialisation point of
a CPU request (hitAction, and per pending message in
removePendingAndRespond) so the embedder can perform stores in order.
invalidate() is called when a line leaves a readable state
(CacheController::updateCacheLine, via
CoherenceProtocolHandler::isReadableState) so the embedder can snoop
its load queue.

[fix] ExternalCPU::processLogic exited the process when the core link
FIFO was full; it now holds the request and retries next cycle
(m_link_full_holds).

[api] FRFCFS_Buffer gains size(), maxSize(), countIf() for the admission
checks.

Effect on results: none for existing presets (none uses ExternalCPU).
Validation: regression identical; llsc under gem5 (Background).
```

Files: `header/ExternalCPU.h`, `src/ExternalCPU.cpp`,
`configuration/ExternalCPU.csv` (new), `header/CacheControllers/BaseController.h`
and `.cpp` (`m_cpu_port`, `setCpuPort`, `demandAdmissionBlocked`,
`demandQueueSize`, the `commit()` calls in `hitAction` and
`removePendingAndRespond`), `header/CacheControllers/CacheController.h` and
`.cpp` (`demandAdmissionBlocked` override; the readability invalidate at the
top of `updateCacheLine`), `header/Protocols/CoherenceProtocolHandler.h`
(`isReadableState`), `header/Protocols/MSIProtocol.h`/`.cpp`
(`isReadableState`), `header/FRFCFS_Buffer.h` (`size`, `maxSize`, `countIf`),
`header/SystemConfigurations/MultiCoreSystem.h`/`.cpp` (`cpu_type`, ExternalCPU
wiring: `getExtCPUs`, `setCpuPort`, `attachCache`).

**Background.** Two experiments set the admission rule. Capping the
embedder at 16 outstanding requests per core (the first port, "queue-bound")
gave 26,427 port refusals and 1,814,092,425 ticks on llsc; blocking the port
only when the L1 cannot take more, with the 64-credit bound, gave 621
refusals and 1,781,162,388. Classic on the same test: 3,243 port blocks,
1,710,679,941. The credit count is not "at most 64 in flight": squashed loads
keep their credit until their response returns, which is what the count is
for.

---

## Commit 4: gem5 bridge, run configuration, preset, build glue

```
gem5: bridge SimObject, cache-hierarchy configs, MultiCoreSystem_gem5 preset

[api] gem5/ holds the Octopus bridge (octopus.cc/hh, Octopus.py): one
SimObject per L1 that owns an ExternalCPU, translates gem5 packets into
addRequest() calls, delivers completions, commits and invalidation
snoops back to the core, and steps the single CacheSim by simulated
time. Data is performed on gem5's backing memory at Octopus's commit
hook; a data operation whose bytes overlap an older unperformed write
from the same core waits for that write (blockedByOlderWrite), which is
the ordering O3 expects from its cache once a store has been sent. The
port is refused while ExternalCPU::isBlocked() and retried from tick()
and from every response.

[config] configs/: OctopusCacheHierarchy (gem5 stdlib hierarchy that
instantiates the bridges), Gem5BaseCacheHierarchy (classic caches sized
from the same CSV preset, for a like-for-like baseline), fs_arm.py (ARM
FS run with --classic, --octopus-config, --octopus-param, --cache-ports,
--max-ticks).

[config] configuration/SystemConfigurations/MultiCoreSystem_gem5.csv: 8 L1s
(4 I + 4 D), split-bus MSI, LLC with MCsim memory, sized from the ARM
challenge XML. Response FIFOs are sized to the outstanding bounds (core
link 64, bus 128) because a full response FIFO aborts instead of
back-pressuring.

[build] Root SConscript for gem5's EXTRAS build: include paths must be
absolute source paths (Dir('header') resolves into the variant
directory), and the library is linked from build/ with an rpath so gem5
is not relinked when the library changes.

Effect on results: none for the standalone presets. Validation: llsc runs
under both hierarchies; the classic baseline built from the CSV preset
reproduces the XML baseline's ticks exactly.
```

Files: `SConscript` (root, new), `gem5/SConscript`, `gem5/octopus.cc`,
`gem5/octopus.hh`, `gem5/Octopus.py`, `gem5/configs/fs_arm.py`,
`gem5/configs/octopus_cache_hierarchy.py`,
`gem5/configs/gem5_base_cache_hierarchy.py`,
`configuration/SystemConfigurations/MultiCoreSystem_gem5.csv` **without** the
two `llc_controller.m_data_handler.m_data_array_*` lines (they belong to
commit 7; `git add -p` the file and skip that hunk, or add the file and
`git reset -p` it).

**Background.** Three start-up failures shaped this commit. (1) gem5's
EXTRAS build put the variant directory on the include path, so
`#include "ExternalCPU.h"` was not found until the SConscript used absolute
source paths. (2) A stale SConscript in the attic was picked up by EXTRAS
and defined a duplicate DebugFlag; hence the attic is ignored. (3) The first
full-system run aborted with `Cannot insert the Msg into lower interface`:
the core-link response FIFO had 16 entries and the O3 core had more than 16
responses outstanding. Responses cannot be back-pressured in this library,
so the FIFOs are sized to what can be outstanding.

---

## Commit 5: split bus with a slot latency of 1

```
bus: SplitBusController with a slot latency of 1

[fix] requestBusStep()/responseBusStep() elected a new message every
cycle, also while one was being held for delivery. With a 1-cycle slot
the new election overwrote the held message, and since the arbiter
erases an entry at election time there was no copy left: the held
message was never delivered and its requester waited forever. Both
steps now elect only when nothing is held, deliver at slot latency-1
and reset the counter after delivery. For slot latencies >= 2 the code
is cycle-equivalent to the old one.

Effect on results: none at the presets' slot latencies. Validation: llsc
with the preset's slots (request 2, response 5) gives the same ticks; with
response slot 1 the run completes instead of hanging; regression
identical.
```

Files: `src/Interconnect/SplitBusController.cpp`.

**Background.** The experiment "does a 1-cycle response slot recover the
LLC hit latency" hung on the first bus transfer. In the old code, with the
slot counter reaching zero in the same cycle as the next election, the
elected message replaced the one still waiting to be sent, and because the
arbiter had already dropped the earlier message from its queue there was no
copy left to retry.

---

## Commit 6: data-array latency at a private cache, one array-access list

```
cache: data-array latency at a private cache; one array-access list

m_data_access_latency is an occupancy: an access returns at once and
closes the array for L cycles, and an access that finds the array closed
parks its data phase. Only the LLC used it, with an arbiter. This commit
makes the model work at an L1 (no arbiter, snooping protocol) and gives
the controller one structure for everything that waits for the array.

[api] One array-access list per controller (m_array_ops, entries of type
DataArrayOp): whole deferred messages and parked byte phases alike, in
arrival order. processDataArrayBuffer serves one entry per free window,
elected by the configured arbiter (keyed on the requesting core, as
before) or oldest first when there is none (arrayElect). It replaces
the byte-phase-only list (m_data_access_buffer / m_data_access_action).
When a victim leaves the array, pipelineFlushLine completes the entries
on that line before the write-back can release the buffered copy (the
former loop in checkReplacements). Completing an access marks the
processing queue dirty (FRFCFS_Buffer::markDirty): the queue's
readiness is state-only and rescans only after a push or pop, and a
completion changes line state without either. The pipelined model of
the next commit adds admission and latency on this same list.

[fix] Without an arbiter, parked accesses were never served (only the
arbiter branch existed), so the second store of a same-line burst hung
the core. The list is served oldest-first when no arbiter is
configured.

[fix] State and data change together. A snoop that hands a modified line
to another core (rows Data2Req/I, Data2Both/S) expands to a write-back
that reads the line, then a state update. With the array closed the
read was parked while the update ran, so the read later met an invalid
line and performWriteBack copied from a null pointer. A message whose
row touches the array is now deferred whole until the array is free and
then run inline (CacheController::deferForDataArray, called by
BaseController::processLogic right after admission). The protocol names
those rows (CoherenceProtocolHandler::needsDataArray; MSIProtocol
implements it as the table's Hit / Data2Req / Data2Both actions via
FSMReader::hasAction). Only table-reported rows are deferred, so a
message that carries bytes but does not touch the array is not delayed.

[check] Every path that reads a line's bytes for a response (hitAction,
removePendingAndRespond, performWriteBack) goes through
BaseController::dataArrayReadFailed: a counted diagnostic, fatal when an
ExternalCPU is attached (the bytes matter there), tolerated for
standalone traces (they carry mock data).
[fix] removePendingAndRespond goes through the readiness check like the
other readers; it read the array inline and aborted when the array was
closed.

Effect on results: none for the lab presets (their L1s run at latency 0,
and the list serves the LLC's byte phases as the old one did).
Validation: harness l1.cc: 8 same-line stores burst ok, 8 loads plus a
remote store ok (hang and crash before); hit latencies unchanged at
latency 0; regression identical.
```

Files: `header/FSMReader.h`, `src/FSMReader.cpp` (`hasAction`),
`header/Protocols/CoherenceProtocolHandler.h` (`needsDataArray`),
`header/Protocols/MSIProtocol.h`/`.cpp` (`needsDataArray`),
`header/CacheControllers/BaseController.h`/`.cpp` (`deferForDataArray` hook and
its call in `processLogic`; `dataArrayReadFailed`, `m_data_read_failures`;
the guarded reads in `hitAction`, `removePendingAndRespond`,
`performWriteBack`), `header/FRFCFS_Buffer.h` (`markDirty`),
`header/CacheControllers/CacheController.h`/`.cpp`
(`DataArrayOp` with its `admitted`/`ready_cycle` fields, `m_array_ops`,
`m_array_op_seq`, `m_arbiter_candidates`,
`m_pipe_ops`, `m_pipe_early_fires`, `arrayEnqueue`, `arrayElect`,
`arbitrated`, `pipelineFire` (MESSAGE and ACTIONS branches), `pipelineFlushLine`,
`deferForDataArray` occupancy branch, `processDataArrayBuffer`,
`checkReadinessOfCache` occupancy branch (enqueues instead of the parked-action
map), `checkReplacements` (flush call), `removePendingAndRespond` override,
`performWriteBack` guard).

**Background, defect 1.** L1 latency 2, core issues store S1 to line X at
cycle t: row M + Store gives Hit, the read runs inline and books the array
closed until t+2. Store S2 to X at t+1: Hit again, `checkReadinessOfCache`
finds the array closed, pushes the action into `m_data_access_buffer` and
returns. From t+2 on, `processDataArrayBuffer` only looked at the arbiter
branch; the L1 has `arbiter_type NULL`; S2 was never served and the core's
store queue filled behind it.

**Background, defect 2.** Same t: S1 books the array until t+2. At t+1 the
bus delivers core B's GetM for X. Row M + Other_GetM gives `Data2Req/I`; the
handler emits WRITE_BACK (no data attached, inserted at the front of the
list) then UPDATE_CACHE_LINE(I). Action 1: `performWriteBack` sees no data,
asks the readiness check, the array is closed, the read is parked. Action 2:
`updateCacheLine` sets X to I, valid false. At t+2 the parked read runs
(once defect 1 is fixed): the tag lookup rejects the invalid way, the line's
data pointer is null, `memcpy` from null. Only transitions to I expose it;
`Data2Both/S` leaves the line readable and the late read succeeds.

**Background, why the LLC never showed it.** The LLC has an arbiter in every
preset; its only transitions to N run on lines already in the write-back
buffer, which the handler reports as always ready; its responses leave lines
in IorS or M; its fills carry bytes in the message. No LLC transition parks a
read and invalidates the same array line, so the design assumption "state
now, bytes later" held there by the shape of that one table.

---

## Commit 7: pipelined data-array model

```
cache: pipelined data-array model (m_data_array_pipelined, m_data_array_ports)

[model] The occupancy model charges the array to the wrong access: a lone
hit pays nothing (only the next access waits) and a burst is serialised
at one access per L cycles. A real SRAM, and gem5's classic L2, charge
every access the latency and start a new one every cycle.

[model] With m_data_array_pipelined=1 the handler never closes the array.
Each cycle the controller admits up to m_data_array_ports waiting
entries of the array-access list (arrayAdmit, the same arbiter-or-oldest
election as the occupancy model), marks each with a ready cycle L later,
and completes them in pipelineStep when that cycle comes; an access may
start the cycle it arrives if a port is free. A deferred message's row
runs at completion (pipelineFire), so the line keeps its current state
for the array latency and a snoop that needs the bytes waits behind the
access producing them. In this model every message that carries bytes
from below or from a peer is an array write and is deferred, whatever
the table says. Single actions that reach the array outside a
completion are parked as ACTIONS entries (checkReadinessOfCache).
[config] Default off; the gem5 preset enables it for the LLC (1 port,
latency 10).

Effect on results: none for the lab presets (off there). Validation:
harness llc.cc: LLC hit alone 10 -> 20 cycles (the array is now charged),
back-to-back hits 10.0 -> 5.0 cycles per extra hit (the response bus slot
becomes the limit); llsc 1,841,019,804 ticks against 1,781,162,388 with
the occupancy LLC (the test is latency-bound and never queues at the
LLC); regression identical.
```

Files: `header/CacheDataHandler.h`, `src/CacheDataHandler.cpp` (the two
parameters, `isReady()` true when pipelined, the four `m_ready_cycle` booking
guards), `configuration/CacheDataHandler.csv`,
`header/CacheControllers/CacheController.h`/`.cpp`
(`m_array_admitted_this_cycle`, `m_pipe_firing`, `pipelinedArray`,
`arrayAdmit`, `messageTouchesArray`, the pipelined admission in
`arrayEnqueue`, `pipelineStep`, the pipelined branches of
`checkReadinessOfCache`, `deferForDataArray`, `cycleProcess`), the two
`llc_controller.m_data_handler.m_data_array_*` lines of
`MultiCoreSystem_gem5.csv`.

**Background.** Measured with one core against a cold LLC: occupancy model,
hit round trip 10 cycles whatever L was, and a 19-hit burst at exactly 10.0
cycles per hit; pipelined, 20 cycles alone and 5.0 per extra hit. The
deferral of whole messages is also what closes the "permission before data"
ordering of the occupancy model for good: the FSM runs when the bytes have
landed.

---

## Commit 8: side buffers bypass the array pipeline

```
cache: lines in the MSHR or write-back buffer bypass the array pipeline

[fix] The COTS handler keeps evicted lines in a pending-write-back map
(PWB) and lines being fetched in an MSHR map. Both are register files,
not the array: the occupancy path reads them inline because
CacheDataHandler_COTS::isReady(address) is true for them, but the
pipelined readiness check never asked the handler and parked every
access for the array latency. An LLC eviction's WriteBack row expands to
WRITE_BACK then UPDATE_CACHE_LINE(N); the parked read of the PWB line
ran 10 cycles after the update had erased the buffer entry, and
CacheController_End2End::performWriteBack copied from a null pointer.

[api] One query, CacheDataHandler::lineLocation() -> ARRAY / MSHR / PWB /
NONE (the COTS override uses the same checks as findline). The
pipelined readiness check runs MSHR and PWB accesses inline; the
pipelined message deferral skips PWB lines (an MSHR line's data message
is the fill, an array write, and stays deferred).

[check] The End2End write-back read goes through dataArrayReadFailed.

[model] Eviction write-backs and hits served from the write-back buffer no
longer pay the array latency; array-resident traffic is unchanged.

Effect on results: none for the lab presets. Validation: harness evict.cc
(LLC 8 KiB, 2048-read stream): segfault before, PASS after under
pipelined LLC and pipelined L1+LLC, occupancy cycle count unchanged;
llc.cc numbers unchanged; llsc pipelined identical at 1,841,019,804 (that
test never evicts); regression identical.
```

Files: `header/CacheDataHandler.h`, `src/CacheDataHandler.cpp`
(`LineLocation`, `lineLocation`), `header/CacheDataHandler_COTS.h`,
`src/CacheDataHandler_COTS.cpp` (override),
`src/CacheControllers/CacheController.cpp` (the two `lineLocation` uses in
`checkReadinessOfCache` and `deferForDataArray`),
`src/CacheControllers/CacheController_End2End.cpp`.

**Background.** First ov2slam run, about one minute after the checkpoint
restore, at the first LLC eviction. Sequence: (1) a fill for line B returns
into a full set; on fire, the update copies victim A into the PWB,
invalidates A's way, queues A for issue, writes B. (2) The replacement check
turns A into a self-addressed Replacement request. (3) Row IorS + Replacement
issues an invalidation to the L1s. (4) The LLC hears its own invalidation
back; row IorS + Own_Invalidation gives WriteBack/N: WRITE_BACK then
UPDATE_CACHE_LINE(N). (5) WRITE_BACK asks for the bytes; the pipelined check
parks the read for 10 cycles although A is in the PWB. (6) The update marks A
invalid; for a buffered line that erases the PWB entry. (7) Ten cycles later
the read finds A nowhere. Every earlier run had an 8 MiB LLC and a working
set that never filled a set.

---

## Commit 9: address interlock: one access per line at a time

```
cache: address interlock, one array access per line at a time

[fix] Since commit 6 a message whose row touches the data array is
deferred whole and its row runs when the access runs. Between pop and
fire the line's state is not yet decided, and a younger message to the
same line, a snoop or a demand request, could pop and act on the line
first. Example at an L1 with a data latency: a load hit to X is deferred
behind a busy array; a remote store's snoop for X arrives, is ready by
the table (S -> I needs no data) and invalidates X; the deferred load
then fires against I and re-runs as a miss. Coherent, but not what
hardware does: an access owns its line from admission to the tag/data
pipeline until it completes, and a later access to that line waits.

Design. The processing queue orders per line; the array-access list only
schedules. A line that has an entry on the array-access list, a deferred
message or a parked byte phase, waiting or admitted, is locked: every
queued message to that line is not-ready, in place, until the entry is
gone (FRFCFS_Buffer::setHold, predicate CacheController::lineHasDeferredOp).
Two properties follow from holding in place rather than taking the
message out of the queue: the message keeps its position, so bus order
and the per-line FCFS gate hold without further mechanism; and the state
a deferred message sees at fire is the state it saw at pop, because
nothing on its line can start in between. When the access completes,
pipelineFire marks the queue dirty and the held messages are rescanned
in the same cycle.

Intake follows the same rule. FRFCFS_Buffer::pushFrontOrdered inserts a
bus message ahead of the core's own requests, as before, but behind
older bus messages, so two bus messages to one line run in bus order
even when the first is held. Upstream inserted every bus message at the
very front; that was equivalent only because a bus message never waited
(no snoop column of the L1 table has a Stall row).

[config] line_interlock(i), default 1; 0 disables the hold for A/B runs.
[check] A deferred message whose row is a Stall at fire time is pushed
back to the queue and counted (m_pipe_requeues). Under the hold this
cannot happen; the counter must stay 0.

Effect on results: this is the first commit that moves the lab presets.
Their LLC uses the occupancy model, whose byte phases park on the array-
access list when the array is busy, and the hold now keeps a younger
same-line request in the queue until that byte phase has run; upstream
decided the younger request at pop and parked its own byte phase behind
the older one. The wait therefore moves from the report's "L2 access"
column into "L2 stall" and same-line accesses complete in arrival order.
Finish-cycle sums over the four cores (Summary.csv), upstream -> this
commit: Directory 3,965,268 -> 3,926,282 (-0.98%), Snoop 4,321,873 ->
4,293,516 (-0.66%); worst-case L2 stall 259 -> 435 and 258 -> 343,
worst-case L2 access 222 -> 143 and 227 -> 116. With line_interlock(i)=0
on every controller both reports are byte-identical to upstream, so
commits 6-8 alone change nothing there.


Validation: harness reorder.cc (load deferred behind a busy array,
remote store next cycle): all four configurations "hit, ordered before
the snoop" with the invalidation right behind the hit; harness stress.cc
(4-8 cores, random loads/stores on 3-4 lines): 15 seed/configuration
combinations pass; other harnesses unchanged. llsc: pipelined LLC
1,815,486,696 ticks (was 1,841,019,804; WFE waits 4,602 vs 5,596),
pipelined L1 latency 2 2,163,095,073 (was 2,135,410,785).
```

Files: `header/FRFCFS_Buffer.h` (`m_hold`, `setHold`, the hold check in
`getFirstReady`, `pushFrontOrdered`), `src/CacheControllers/BaseController.cpp`
(`pushFrontOrdered` in `addRequests2ProcessingQueue`),
`header/CacheControllers/CacheController.h`/`.cpp` (`lineHasDeferredOp`, the
`setHold` lambda in the constructor, `m_line_interlock` and its parameter
read, `m_pipe_requeues` and the re-queue branch in `pipelineFire`).

**Background, the reorder** (harness `reorder.cc`, L1 latency 10). Core A
loads Y at t, the array is booked until t+10; A loads X at t+0.5, the hit is
deferred behind Y; core B stores to X at t+2, its snoop reaches A at t+6.
Before the fix: A invalidates X at t+6, the deferred load fires at t+11
against I and completes at t+29 as a miss, after B's store (t+24). After:
the load hits at t+21, the invalidation follows at t+19-21, B's store
completes at t+23.

**Background, why a waiting message must keep its queue position** (stress
seed 2, LLC ctl 10, line 0x400040; observed with an earlier form of the
interlock that popped the younger message and parked it behind the older
access, re-queueing it at the tail if its row had become a Stall). Bus
order: GetM 3472 from L1 5, then GetS 3473 from L1 7. 3472 was popped in
state M while an older access to the line was in flight and parked; by the
time it ran, that access had moved the line to IorS_d, where GetM stalls,
and 3472 went to the queue tail. 3473 had arrived meanwhile and now sat
ahead of it. When the line reached IorS the LLC served 3473 first (SendData)
and 3472 second (SendData/SetOwner/M). But L1 5, having seen 3473 on the bus
while waiting for its own data (IM_d + Other_GetS -> SaveReq), had already
committed to answer L1 7 itself: on its OwnData it did Hit/Data2Both. L1 7
received two data copies; the second met state S: `MSIProtocol: Fault
Transaction (state S, OwnData)`. In the gem5 llsc run the same inversion
surfaced as `LLCMSIProtocol: Stall Transaction`. Held in place, 3472 stays
ahead of 3473 and the gate keeps 3473 behind it; the LLC then serves them in
bus order, which is what L1 5 assumed.

**Background, why intake must respect bus order** (stress seed 2, pipelined
L1 latency 2, line 0x400080; observed with the hold in place but upstream's
front insertion). L1 4 issues GetM 15918; it is delivered everywhere at
cycle 76516. Every controller processes it except the owner L1 5, where it
is held behind L1 5's own deferred store. At 76518 L1 6's GetS 15920 for the
same line arrives at L1 5 and front insertion puts it ahead of 15918. The
store fires that cycle, the hold lifts, and the scan finds 15920 first:
M + Other_GetS gives Data2Both/S, L1 5 goes to S and sends the line to L1 6.
Then 15918 is popped: S + Other_GetM gives I with no data. The LLC, which
processed both in bus order, had set owner 4 on 15918 and on 15920 moved to
IorS_d expecting owner 4 to supply L1 6. L1 4 never receives data; its queue
ends with three CPU requests stalled in IM_dSI and every other structure
empty (`OCTOPUS_DUMP_AT=95000`). With `pushFrontOrdered`, 15920 queues
behind 15918 and the owner answers L1 4 first.

---

## Commit 10: directory-protocol defects exposed by the interlock

Apply after commit 9; required before commit 11, which hangs the lab's
Directory preset without it.

```
protocol: membership-aware last PutS/InvAck; MSI_directory store rows

Two latent defects in the directory protocol. Both assume an ordering the
LLC no longer guarantees once an access can wait for the array with its
line locked (commit 9) and, from commit 11, once the LLC decides a
request when its array access runs rather than at pop.

[fix] LLCMSIDirectory / LLCMOESIDirectory classified a PutS or InvAck as
the last one by list size alone ((size - 1) == 0), without checking that
the sender was still in the sharer list. That assumed the message was
processed before a later request's invalidation could remove its sender.
Waiting behind the line's array accesses, a PutS ran after that removal;
the size test drove the line to I while another sharer remained, and the
next GetM was answered from I with an ack count taken from the stale
list and no invalidation sent, so its requester waited for an ack that
could never come. isLastFromSender() requires membership and size 1,
which is also how the primer defines PutS-Last.

[fix] Protocols_FSM/MSI_directory.csv rows IM_aI and IM_aSI, the states a
requester enters when a forwarded request arrives while it waits for
its invalidation acks, handed the line on at the last ack
(Data2Req/Ack_dec/I, Data2Both/Ack_dec/I) without performing the
requester's own store. MESI_directory.csv has the same rows with the
Hit; MSI's lacked it. The window, a forward between a requester's data
and its last ack, was almost never open with the LLC answering at pop;
with the LLC's outgoing traffic reordered by deferral and hold it opens,
and the requester's store then stays pending forever with every queue
empty.

Effect on results: on top of commit 9 the Directory preset's finish-
cycle sum moves 3,926,282 -> 3,928,029 (+0.04%), so the defects were
already altering that preset's timing without hanging it; the Snoop
preset does not use these classes and is unchanged. With commit 11 and
without this commit the Directory preset hangs in both of the sequences
below.

```

Files: `header/Protocols/LLCMSIDirectory.h`, `src/Protocols/LLCMSIDirectory.cpp`
(`isLastFromSender` and its two call sites), `src/Protocols/LLCMOESIDirectory.cpp`
(two call sites), `Protocols_FSM/MSI_directory.csv` (rows `IM_aI`, `IM_aSI`;
the file keeps its BOM and line endings, the diff is those two rows).

**Background, the stale sharer list** (Directory preset, line 0x7ffeab880880,
LLC ctl 10, from the hang run's `OCTOPUS_TRACE_ADDR` trace; cores are
ctl 0-3). The L1 directory table is `MSI_directory.csv`, the LLC's
`MSI_LLC_directory.csv`.

- 238476: core 3 holds the line in S and evicts it: PutS 60012 goes to the
  LLC, core 3 enters SI_a. At the LLC the line has array accesses pending,
  so 60012 is held in the queue (238478).
- 238540: the LLC runs core 1's GetM 59984 (S + GetM: SetOwner, SendData,
  SendInv): the sharer list becomes {1}, invalidations go to the old sharers;
  core 3 receives its Inv at 238541 in SI_a (InvAck2Req, II_a). Same cycle,
  core 0's GetS 60003 pops in M: ClearOwner, FwdGetS, IncSharers, S_d; list
  {1, 0}.
- 238554-238580: core 1 answers the forward (Data2Both) and evicts; its data
  reaches the LLC as PutM_Data from a non-owner (owner was cleared):
  PutAck/DecSharers removes 1, the pending GetS is completed, the line is S
  with list {0}.
- 238580: core 3's PutS 60012 finally pops, in S. Core 3 is no longer in the
  list, but the list has one entry, so the size-only test classifies it as
  last_PutS: PutAck/DecSharers/I. DecSharers finds no entry for 3; the line
  is I at the LLC while core 0 holds it in S and the list still says {0}.
- 238580: core 2's GetM 60011 pops in I: SetOwner/SendData/M. SetOwner sets
  the owner field before SendData computes the ack count, so the count is
  taken from the list {0}: one ack expected from core 0. The I row sends no
  invalidation (I has no sharers), so core 0 is never told.
- 238605: core 2 receives the data with a nonzero ack count (IM_ad ->
  AckNum_set/IM_a) and waits for core 0's InvAck. Core 0, still in S, issues
  its own GetM 60019, which the LLC forwards to owner 2; core 2 in IM_a
  saves it (IM_aI) to answer after its acks arrive. Neither happens: core 2
  waits for an ack nobody sends, core 0 waits for core 2's data. The LLC's
  queue empties and the run stops with both stores pending.

With isLastFromSender, 60012 is a plain PutS (PutAck/DecSharers, line stays
S, list {0}); core 2's GetM then runs the S row, which invalidates core 0
and expects exactly its one ack. The defect does not depend on the hold as
such: any PutS from a core that has since been invalidated, arriving at an S
line with exactly one other sharer listed, takes the wrong row. The hold and
the LLC's deferral change the order enough for the lab trace to hit it.

**Background, the missing Hit** (same preset, found after the first fix;
core 1's store 29758 at cycle 95283 in the trace). Requester R in IM_a (data
received, acks outstanding) is forwarded another core's GetM: SaveReq takes
it to IM_aI. Its last InvAck arrives: the row performed Data2Req and
Ack_dec and moved to I, forwarding the line on without ever executing R's
own store, which stays in the pending table. Nothing else references it, so
the simulation ends with that request outstanding. The MESI table's
identical rows read `Hit/Data2Req/Ack_dec/I`; the primer's IM^AI row also
performs the store before handing the block on.

---

## Commit 11: the LLC decides a hit when its array access runs, as the L1 does

Apply after commit 10. Commits 12 and 13 do not depend on it.

```
llc: defer array-touching rows whole, like the L1

[model] LLCMSIProtocol::needsDataArray and
LLCMSIDirectory::needsDataArray report the rows whose actions read or
write the array (SendData, SendExeclusiveData, SaveData); every LLC
protocol class derives from one of the two. Before this commit only the
L1 protocol reported such rows; the LLC ran its row at pop and parked
only the data phase, a serial tag-then-data shape without the lock on
the line between the two, correct only because no LLC row invalidates
what its own parked read needs. Now a hit is decided when its array
access runs at both levels, the two controllers take the same path
through deferForDataArray, and the array-access list carries the same
kind of entry at both. Checked against the tables: in MSI_LLC.csv and
MESI_LLC.csv no array-touching row sends a message that does not itself
need the data, so deferring the decision delays nothing that could have
left earlier. MSI_LLC_directory.csv has two rows where it does: S + GetM
sends the invalidations (SendInv) with the data, and M +
PutM_Data_fromOwner sends the PutAck with the SaveData; both now leave
when the array access runs rather than at pop, i.e. one array latency
later than a tag-then-data pipeline would send them. That is the model's
one-access shape, not a defect, and it is part of the Directory preset's
shift below.

Two levels, cleanly separated, after this commit:

Level 1, the processing queue, decides whether a message may START on
its line: first-come per line among demand requests, bus order among
bus messages, and no start while an older access to the line is on the
array-access list (commit 9). Once released, a message has a fixed
place in its line's history.

Level 2, the array-access list, decides WHEN an access gets the array:
a resource policy across lines and cores, oldest-first or the
configured arbiter (FCFS, round-robin, TDM keyed on the requesting
core), within the array's ports and latency (commits 6 and 7). Level 1
guarantees at most one access per line in level 2, so level 2 may
reorder across lines freely.

Effect on results, on top of commits 9 and 10: Directory 3,928,029 -> 3,922,864
(-0.13%), Snoop 4,293,516 -> 4,273,589 (-0.46%); worst-case L2 stall
397 -> 462 and 343 -> 420, worst-case L2 access 143 -> 116 and 116 ->
98. Cumulative against upstream: -1.07% and -1.12% of finish cycles,
with the wait moved from the L2 access column into L2 stall. gem5
preset (pipelined LLC): llsc tick counts unchanged from commit 9
(1,815,486,696 and 2,163,095,073); the pipelined path already deferred
every message carrying data and held the line for the parked byte
phases, so only the decision point moved and nothing observed the
difference in that test.

Validation: harnesses l1, llc, evict, reorder unchanged; stress 15
seed/configuration combinations pass; regression as measured above;
llsc as above.
```

Files: `header/Protocols/LLCMSIProtocol.h`, `src/Protocols/LLCMSIProtocol.cpp`,
`header/Protocols/LLCMSIDirectory.h`, `src/Protocols/LLCMSIDirectory.cpp`
(`needsDataArray`).

**Background: why the LLC is changed separately from the L1.** Commit 6
made whole-message deferral a controller mechanism keyed on the protocol's
`needsDataArray`, and enabled it only for the L1 protocol, whose snoop rows
invalidate the line their own data phase still needs. The LLC's tables have
no such row, so it stayed correct deciding at pop, and it was kept there
until the interlock existed, because the lab presets exercise the LLC and
were the regression check for the earlier commits. With the hold in place
(commit 9) deciding at fire is safe at the LLC too, and this commit makes
both controllers take the same path.

**Background: why this is the hardware shape.** A cache has one tag/data
pipeline per bank; an access is admitted once, owns its line until it
completes, and the array's bandwidth is scheduled among admitted accesses by
the array's own arbiter. The hold is the ownership; the single list with the
configured arbiter is the array scheduler; deferring the whole message means
the model's data phase never has to look the line up again by address,
which is the one place the model differs from a pipeline that carries the
selected way forward.

**Background: why the lab numbers move down, not up.** The hold and the
whole-message deferral do not add latency to any single access; they change
which of two same-line requests goes first and where its wait is booked.
The per-request reports differ in nearly every row, the finish cycles by
about one percent, and the worst-case L2 access column falls because a
request no longer sits decided-but-unserved behind an older access's byte
phase.

---

## Commit 12: diagnostics

```
debug: event trace, state dump, and context in protocol fault messages

[debug] Env OCTOPUS_TRACE_ADDR=<addr> prints every event on that line at every
controller (intake, pop, hold and what holds it, park, fire, requeue,
admit-fail); =1 traces every line. Env OCTOPUS_DUMP_AT=<cycle> dumps
each controller's queue (with FSM readiness and hold status), its
pending requests and its array-access list at that cycle; the same
address filter prints the memory interface's request and read-callback
events. All off by default and cost one integer compare per event.

[check] `LLCMSIProtocol: Fault Transaction is detected` and the Stall variant,
and MSIProtocol's Fault, now print controller id, line state and the
message (id, source, owner, type, data), which is what turns "a fault
somewhere" into a five-line diagnosis.

[debug] BaseController gets a virtual traceMsg() hook so the intake and pop
sites can report without knowing about CacheController.
```

Files: `header/CacheControllers/BaseController.h` (`traceMsg` hook),
`src/CacheControllers/BaseController.cpp` (the `traceMsg` calls in
`processLogic` and `addRequests2ProcessingQueue`),
`header/CacheControllers/CacheController.h`/`.cpp` (`traceMsg`, `dumpState`,
`m_trace_addr`, `m_dump_at`, `m_trace_held`, the env reads in the constructor,
the trace calls in `arrayEnqueue`, `arrayAdmit`, `pipelineFire`, `deferForDataArray` and the
hold lambda), `header/FRFCFS_Buffer.h` (`itemAt`, `stateAt`),
`src/Protocols/LLCMSIProtocol.cpp`, `src/Protocols/MSIProtocol.cpp`,
`src/MCsimInterface.cpp` (trace lines).

**Background.** Every failure in commit 9 was first reported as a bare
"Fault Transaction is detected" or as a silent hang. The lost-message case
was found only by tracing intake and pops across all controllers: the
message appeared at eight controllers and vanished at one.

---

## Commit 13: harnesses, this document, gem5 patch, README

```
docs: harnesses, commit plan, gem5 patch, README

[docs] gem5/harness/: single-binary tests against build/libOctopus.so with no
gem5 (build.sh): l1 (hit latencies, same-line burst, loads plus remote
store), llc (hit alone and back-to-back), evict (LLC eviction stream),
reorder (snoop vs deferred hit ordering), stress (multi-core random
contention, hang and fault detector), seq (back-to-back timing).

[docs] gem5/COMMIT_PLAN.md: the commit-by-commit rationale with the trigger
sequence behind each fix, and the open improvements (data owned by Octopus
first among them). gem5/cmds.md: environment and command notes.

[docs] gem5/patches/: the gem5-side change the integration needs
(ISA::globalClearExclusive must not squash another core's pipeline),
as a patch against v25.1.0.1.

[docs] README: how the gem5 integration is laid out, built and run, the
parameter overrides, the harnesses and the debug aids.
```

Files: `gem5/harness/*.cc`, `gem5/harness/build.sh`, `gem5/cmds.md`,
`gem5/patches/*.patch`, `gem5/COMMIT_PLAN.md` (this file), `README.md`,
`.gitignore`.

---

## Executing the plan

For each commit: stage the listed files; for the mixed files use
`git add -p <file>` and take only the hunks named; build
(`cmake --build build -j8`); run `/tmp/oct_regress.sh`; then commit with the
message above. The regression is byte-identical through commit 8 (no lab
preset exercises the changed paths, and the array-access list serves the
occupancy model's byte phases exactly as upstream's list did). Commit 9 is
the first that moves it, commits 10 and 11 move it again, each by the
amount recorded in its message; the final numbers are those under commit
11. Run the harnesses after commits 6, 7, 8, 9 and 11
(`gem5/harness/build.sh`, then each binary with `/tmp/octlog`). The two
llsc gem5 runs are the end-to-end check after commits 9 and 11.

If splitting `CacheController.cpp` across commits 6-12 by hunk proves too
fiddly, an acceptable fallback is to fold commits 6 and 7 into one
("data-array latency: occupancy fixes and pipelined model") and commit 12
into commit 9 ("address interlock, with diagnostics"); the messages above
concatenate cleanly.

---

## Potential improvements

One item is planned; everything else that came up during the port is either
done or recorded in the retired work log.

**Data owned by Octopus** (Stage 4 of the retired LL/SC plan, its phases
2-4). Today the bridge performs every load and store on gem5's backing
memory at the L1's commit hook (`Octopus::performData`, one `sendAtomic`),
and the L1 carries state and timing only. The step: carry bytes, size and
byte enables into Octopus; make the L1 data commit explicit on its own line
copy; move the external memory boundary to the LLC, where a fill reads gem5
memory once per line and a dirty eviction writes it back, both as temporary
plain packets keyed by `msg_id`; keep the LL/SC reservation in the L1
(installed at the load-exclusive's commit, cleared by every overlapping
committed write and by invalidation, checked and consumed at the
store-conditional's commit with the result set on the packet); run swaps
and atomics with gem5's `AtomicOpFunctor` on the line once the L1 holds it
in M. Retires the `sendAtomic` on the CPU path, the same-core hold-back
(`blockedByOlderWrite`: a younger load then reads the L1's own bytes after
the older store wrote them) and the gem5 `isa.cc` patch (the monitor clear
no longer goes through the ISA). Invariants: a packet completes exactly
once; a write commits only after writable ownership; an invalidation
reaches O3 through the timing snoop before any later data can be observed.
Validation: llsc (all five sub-tests), the harnesses, and a memory image
compared against a classic run of the same checkpoint at exit.
