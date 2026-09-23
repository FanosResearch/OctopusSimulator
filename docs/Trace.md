# Raw event trace (`OCTOPUS_TRACE`)

Opt‑in, zero cost when unset. Set `OCTOPUS_TRACE=<file>` and every resource event of **every**
message — CPU requests and the non‑request traffic the per‑request report cannot see
(write‑backs, back‑invalidations, cache‑to‑cache supplies, fills, LLC evictions) — is appended to a
binary file at simulation speed (a2time01 ×4, real LLC, OoO 8: 2.3 M records, 55 MB, no
measurable slowdown). The per‑request `LatencyReport` (docs/Logger.md) stays the authoritative
latency decomposition; the trace is what the visualizer's resource lanes are built from.

## Record (32 bytes, little‑endian; magic `OCTV2`)
| field | meaning |
|---|---|
| `cycle` u64 | global core cycle = `ClockManager` time / 100 + 1 (the 50 ns buses stamp in core‑cycle units too) |
| `msg_id` u64 | the message id (a demand request keeps it across its life; write‑backs and evictions have their own — see `docs/MessageEncoding.md`) |
| `addr` u64 | the message's address (so lanes and transitions can be filtered by line) |
| `resource` u8 | `Logger::Role`: 0 CPU, 1 L1, 2 REQ_BUS, 3 RESP_BUS, 4 LLC, 5 MEM_BUS, 6 DRAM, 8 SVC_BUS, 9 ARRAY, 10 LLC_QUEUE, 11 FSM |
| `phase` u8 | 0 ENTER, 1 SERVICE, 2 EXIT — for FSM records: the FSM **event id** |
| `kind` u8 | `Message::Kind`, assigned by the producer: 1 DEMAND, 2 GETS, 3 GETM, 4 PUTM, 5 INV, 6 MEM_READ, 7 EVICT, 8 WB_DATA, 9 WB_INV, 10 SUPPLY, 11 SUPPLY_DEFERRED, 12 RESP, 13 FILL, 14 FILL_ROLLBACK, 15 MEM_WRITE; 0 = untagged |
| `core` u8 | `Message::owner` (requester for requests/responses/supplies/fills, evicting L1 for write‑backs, LLC for invalidations) |
| `comp` u16 | component id (L1 0–3, LLC 10, DRAM 100; bus 0 = L1↔LLC, 1 = memory bus) |
| `flags` u16 | ARRAY: low byte = the `ControllerAction::Type` that claimed the port (1 HIT_Action = read for a response, 4 WRITE_BACK = read for a write‑back / supply, 6 WRITE_CACHE_LINE_DATA = write of a fill / write‑back / snooped supply); bit 8 (0x100) = **cut‑in**, the port timer was still busy when this access was admitted; bit 9 (0x200) = the line was MSHR‑resident, bit 10 (0x400) = PWB‑resident at the claim (see below). FSM: `old_state << 8 | new_state` |

### FSM records (coherence transitions)
Every protocol reports each transition through one hook, `DebugPrint::transition` (docs/Debugger.md):
an enabled debugger prints the readable `old --event--> new` line, and when `OCTOPUS_TRACE` is set
the same call appends a `Role::FSM` record: `comp` = the controller (L1 id or LLC id), `phase` =
event id, `flags` = old/new state ids, `msg_id`/`kind`/`core` = the message that caused it. A
Stall row is recorded as `old → old`. State and event names are not in the records: at close the
Logger writes `<file>.names` with one `comp <id> states a,b,…` and one `comp <id> events …` line
per controller (ids are the FSM CSV order), which the converter uses to resolve names.
`convert.py --trace` turns them into `fsm.parquet` (`cycle, comp, addr, line, msg_id, kind, core,
event, old, new`), the source of the viewer's per‑line transition table.

## Where each event is produced
| resource / phase | site | meaning |
|---|---|---|
| CPU ENTER / EXIT | `CPU` | issue / retire (kind DEMAND at issue, RESP at retire) |
| LLC_QUEUE ENTER | `BaseController::addRequests2ProcessingQueue` | arrival in a controller's processing queue (comp = that controller: an L1 for its CPU's requests, the LLC for bus traffic) |
| L1 / LLC ENTER | `BaseController::processLogic` | admission (popped ready from the queue) |
| L1 / LLC EXIT | `hitAction`, `removePendingAndRespond`, `performWriteBack` | a data message emitted (RESP, SUPPLY, WB_*) |
| REQ_BUS / RESP_BUS ENTER | `SplitBusController` election | **grant** = start of the slot |
| REQ_BUS / RESP_BUS / MEM_BUS / SVC_BUS EXIT | `BusController::broadcast` / `send` | transmitted (end of the slot); the service channel (INV) and the point‑to‑point memory bus have no election event |
| ARRAY SERVICE | `CacheController::checkReadinessOfCache` | the data‑array port is claimed; `flags` = action (+ cut‑in bit) |
| DRAM ENTER / EXIT | `MainMemoryController` / `MCsimInterface` | request accepted / read data returned (writes are fire‑and‑forget: ENTER only) |

Kinds are attached where the message is created (`MSIProtocol` GetS/GetM/PutM and the four
Data2Req/Data2Both cases, `LLCMSIProtocol` SendData/GetData/IssueInv/WriteBack, the fill in
`MCsimInterface`/`MainMemoryController`, the replacement in `CacheController`, the rollback fill in
`CacheController_End2End`, RESP at `hitAction` and at the pending‑request response). The encoding
they are derived from is validated in `docs/MessageEncoding.md`.

## Giant runs: trace window and streamed conversion
A record is 32 B, so a billion-cycle SPLASH run would write tens of GB. Two levers:
- **`OCTOPUS_TRACE_WINDOW=t0:t1`** (core cycles) makes the writer keep only events inside that
  window; everything else in the run is unchanged. Use it to trace the region around a worst case
  found in the `LatencyReport`.
- **Streamed conversion**: `occupancy.py` (and `convert.py --trace`) walk the file chunk by chunk
  (64 K records, ~2 MB) with bounded memory. Grants, DRAM reads and queue entries whose end lies in
  a later chunk are carried over, so chunk boundaries are invisible. Output goes to Parquet parts
  `occupancy_NNN.parquet` / `fsm_NNN.parquet` of `--part-rows` rows (default 2 M; parts are
  cycle‑ordered and sorted by start inside), which DuckDB and the server read as one table through a
  glob; a run that fits in one part keeps the plain `occupancy.parquet` / `fsm.parquet` names.
  `--t0/--t1` convert only a cycle window of a full trace (the chunk index skips the rest); the
  row‑consistency validation then checks only requests that lie entirely inside the window, and
  starts/ends cut by the window edge are counted and dropped. a2time01 (3.1 M records) converts
  in 6 s; the per‑record cost is linear.
- **Viewer**: the server discovers either layout per run (`occupancy.parquet` or
  `occupancy_*.parquet`, same for `fsm`) and queries the parts as one table; Parquet zone maps on
  `start`/`cycle` keep window queries cheap because each part is cycle‑ordered.

## File layout
`magic "OCTV1\0\0\0"`, then records in 64 K‑record chunks, then an index (`offset, count,
cycle_min, cycle_max, id_min, id_max` per chunk), then a trailer (`index_offset u64, n_chunks u64,
magic`). A file without a trailer (killed run) is still readable sequentially.
`tools/octoviz/tracefile.py` reads it (`read_all`, and `read_window(t0, t1)` which decodes only
the chunks whose cycle range intersects the window — the path for billion‑cycle SPLASH traces).

## Occupancy table (`tools/octoviz/occupancy.py`, or `convert.py --trace`)
One row per busy interval `[start, end)` of a resource instance, for every message:
`resource, comp, start, end, kind, core, msg_id, flags, cutin, addr`.

| resource | interval | comp |
|---|---|---|
| REQ_BUS / RESP_BUS | `[ENTER, EXIT]` of the same `msg_id`+`kind` (grant → transmitted; the k‑th ENTER pairs with the k‑th EXIT of that key) | 0 |
| SVC_BUS | `[EXIT − 1, EXIT]` (no election event; the request‑bus slot is one core cycle, and consecutive INVs are observed one cycle apart) | 0 |
| MEM_BUS | `[EXIT − A_req, EXIT]` for MEM_READ / MEM_WRITE, `[EXIT − A_res, EXIT]` for FILL. The point‑to‑point bus is full duplex, so **comp = direction**: 0 toward memory, 1 toward the LLC | 0 / 1 |
| ARRAY | `[SERVICE, SERVICE + A_LLC]` for the LLC (comp 10); `[SERVICE, SERVICE + A_L1]` (default 0) for an L1; a write into a PWB‑resident line (flag 0x400, action 6) is a register merge that never touches the port → zero width. Columns `cutin` and `reg` (1 MSHR, 2 PWB) carry the flags | 0–3, 10 |
| DRAM | `[ENTER, EXIT]` by `msg_id` for reads; writes `[ENTER, ENTER + 1]` (accept only). MCsim's bank is not in the trace, so DRAM is one lane in which reads legitimately overlap | 100 |
| LLC_QUEUE | `[LLC_QUEUE.ENTER, controller ENTER]` by `comp`+`msg_id`+`kind` = queue residency (a set, not a server) | 0–3, 10 |

Defaults `A_req 2, A_res 5, A_LLC 10, A_L1 0` (the sweep configurations).

### Time base: the trace vs the Logger rows
The Logger stamps an event with the **core clock count** at the moment it is logged. The buses tick
before the CPUs within a timestamp, so every bus event (grant, broadcast, memory‑bus transfer) is
recorded one cycle earlier in the `LatencyReport` than on the global clock the trace uses; LLC,
array and DRAM events agree. The occupancy builder therefore shifts the four bus resources by −1 by
default so lanes line up with the request rows (`--global-clock` keeps the physical stamps). This is
a fixed one‑cycle attribution between Req‑Bus/L2‑Stall and Resp‑Bus/L1‑Access in the rows, totals
are unaffected; moving the Logger to the global clock is a results‑invalidating change and is
deferred until the next full re‑collection.

### Array cut‑ins (a modelling fact worth knowing)
`CacheDataHandler_COTS::isReady(addr)` admits an access whose line sits in the MSHR or the
pending‑write‑back buffer even while the port timer is busy (the data lives in the register), but
the fill / write‑back it performs still resets the port timer to `now + A_LLC`. So a DRAM fill, an
LLC eviction reading its PWB entry, or a hit served from the MSHR **cuts in** ahead of the access
in progress and stretches the port's busy window. The trace flags these claims (bit 0x100), the
occupancy table carries them as `cutin = true`, and they are the only ARRAY intervals allowed to
overlap. On a2time01 (real LLC, OoO 8): 4 232 cut‑ins out of 45 000 LLC port claims, almost all
FILL and MEM_WRITE (PWB) accesses. A PCC‑perfect LLC (`perfect_llc=1`, oversized) has no evictions and no DRAM, but each line's first touch is still a cold miss served by a same‑cycle loopback fill (there is no pre‑warm), so it keeps the fill cut‑ins.

### Validation (`--validate <octoviz.parquet>` of the same run)
1. *Consistency with the request rows* — for every demand request: its GetS/GetM transfer ends
   the Req‑Bus stage; the response carrying its id (LLC RESP or another L1's SUPPLY) ends the
   Resp‑Bus stage; the LLC's array read of a stable‑state hit starts at the array grant; the DRAM
   interval has the row's DRAM length; the fill transfer ends where the row's Mem‑Bus+DRAM block
   ends; the memory read leaves between LLC admission and DRAM acceptance.
2. *No double booking* — a resource instance never hosts two overlapping intervals, exempting the
   queue (a set), DRAM (parallel banks), cut‑ins and zero‑width L1 array claims.

a2time01 (real LLC, OoO 8, 275 304 requests, 873 245 intervals): 0 mismatches on all six checks,
no double booking.
