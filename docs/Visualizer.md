# Octoviz — request / resource timeline viewer (prototype)

`tools/octoviz/` turns a run's `newLogger/LatencyReport_C*.csv` into an indexed store and shows
the run as a **pipeline**: which request was in which stage at every cycle, and which request
occupied each shared resource. It is a local web app (data never leaves the machine).

## Why it works without new tracing
Every Logger row tiles a request's life exactly (`docs/Logger.md` §5): `issue = Ready + CPU`, then
L1‑Stall, Req‑Bus, L2‑Stall, [Mem‑Bus, DRAM], L2‑Access, Resp‑Bus, L1‑Access, whose sum is
`Total`. `convert.py` derives absolute start/end cycles of every stage and the **occupancy**
intervals of the shared resources (the request bus slot, the array read, the memory bus legs, the
DRAM excursion, the response bus transfer). The tracker columns (class, arrival/stall state,
younger‑ahead, array‑ahead, oldest) come along unchanged. What rows do **not** contain is
non‑request traffic (write‑backs, invalidations, cache‑to‑cache supplies, LLC evictions): resource
lanes therefore show occupancy *by requests*; gaps are "idle or held by something else". An opt‑in
raw event trace (phase 3) will fill those in and give per‑bank DRAM lanes.

## Files
| file | role |
|---|---|
| `tools/octoviz/convert.py <newLogger dir> [--trace <trace.bin>] [--t0 T0 --t1 T1] [--part-rows N]` | CSV → `octoviz.parquet` (ZSTD, sorted by issue; ~35 B/request); with `--trace` also the occupancy and FSM tables (all traffic, validated against the rows), streamed chunk by chunk into Parquet parts so giant traces convert with bounded memory |
| `tools/octoviz/occupancy.py <trace.bin> [-o dir] [--t0 T0 --t1 T1] [--validate <octoviz.parquet>]` | the trace → occupancy/FSM builder on its own, with the two validations (docs/Trace.md) |
| `tools/octoviz/tracefile.py` | reader for the binary trace (`read_all`, windowed `read_window`) |
| `tools/octoviz/server.py --root <dir>` | finds every `octoviz.parquet` under `<dir>`, serves the UI at `http://localhost:8765/` and the JSON API |
| `tools/octoviz/static/index.html` | the viewer (vanilla JS + canvas, no build step) |

Prerequisites: Python 3.9+ with `pip install duckdb numpy` (nothing else; the UI has no build
step and needs no network) and a browser. Everything runs on the user's machine.

The one‑command way is the root script: `./octoviz.sh view <workload_dir>` simulates the
workload with the trace on, converts it and opens the viewer; `run`, `convert` and `serve` are
the same steps separately (`./octoviz.sh` prints the options: `WINDOW=t0:t1` for giant runs,
`TRACE=0` for rows only, `PORT`, `TIMEOUT`, extra simulator `-p` flags after `--`). The manual
steps below are what it runs.

```bash
# convert one run, or all runs of a sweep
python tools/octoviz/convert.py results/pcc_par/wl_eembc/0_8/a2time01-trace/newLogger
for d in results/pcc_par/wl_eembc/*/*/newLogger; do python tools/octoviz/convert.py "$d"; done
python tools/octoviz/server.py --root results/pcc_par/wl_eembc      # then open http://localhost:8765/
```
Dependency: Python 3 + `duckdb` (`python -m pip install duckdb`).

## Views
- **Pipeline (Gantt)** — one row per request in the window, sorted by issue; coloured segments per
  stage; ▲ marks the cycle the request became the *oldest* of its core; hover for the full row,
  click to open the inspector.
- **Resource lanes** — request bus, LLC array port, memory bus, DRAM, response bus; each segment is
  the occupying request coloured by core. Bright = the filtered set, dim = every other request in
  the window ("context"), so *who is ahead of me* is visible.
- **Inspector** — the row's fields and every request on the same 64‑B line within ±2000 cycles
  (coalescing / transient context); click one to jump to it.
- **Minimap** — request density over the whole run; click to move the window.

## One page or many windows
The views share one page, which gets busy. Two controls sit in the bar under the filters:

- **click a view's name** to hide or show it on this page;
- **click its ⧉** to open it in a **window of its own**, where it fills the screen. Opening a
  view this way hides it on the main page, so the two together act as a layout: keep the pipeline
  in the main window, put the lanes on a second monitor, and open the transition table beside it.

Every window shows the same page with `?solo=<view>` added, so a popped‑out window is also a
shareable link and keeps its own URL hash. Windows of the same run talk over a `BroadcastChannel`:
panning or zooming, changing any filter, switching run, or clicking a request in one window moves
all the others to match, so the comparison is always of the same cycles. The header of a
popped‑out window says whether it is linked (a browser without `BroadcastChannel` still works,
just unlinked).

## Level of detail (billions of cycles)
The UI only ever holds one window. Below `--lod-cycles` (default 40 000) the server returns rows;
above it, per‑bin aggregates (requests issued, coalesced, misses, max Total, and per‑resource
occupancy cycles) computed by one `GROUP BY` in DuckDB, drawn as density/utilisation. Zoom and pan
(wheel/drag) re‑query with a 180 ms debounce. Queries are predicates on Parquet zone maps, so a
line filter on a 98 M‑request SPLASH run still answers in milliseconds.

## Filters (all AND‑combined, kept in the URL hash so a view can be shared)
| knob | meaning |
|---|---|
| cycle from/to | window; requests *overlapping* it are included |
| address + mask (hex) | `(addr & mask) == (address & mask)`; default mask `…ffc0` = 64‑B line, `f…f` = exact word — same semantics as the Debugger's `addr_filter`/`cond_addr` |
| request id / id range | global `msg_id` (the `RequstID` column), so it joins with dprint traces |
| cores | requester core |
| class | L1 hit / stable / transient / coalesced / miss (from `LLC Arrival State` + DRAM columns) |
| used resource | only requests that occupied the ticked resources |
| stall state | `LLC Stall State` ids (1 NE_d, 2 NM_d, 6 IorS_a, 7 MN_d, 8 S_d, 9 I_d, 10 N_a, −2 never) |
| oldest only / resp‑ahead ≥ k | requests that became oldest; requests with ≥ k younger own responses granted ahead |
| context lanes | draw the unfiltered occupancy dimmed under the filtered set |

## Assumptions baked into the derived intervals (adjust in `convert.py`)
`A_req = 2`, `A_res = 5`, `A_LLC = 10` (the request‑bus slot, response‑bus slot and array access
of the current configuration). The memory bus is drawn as one interval (the row does not separate
its two legs). The array read overlaps the start of the Resp‑Bus stage because the LLC stamps
SERVICE and EXIT in the same cycle.

## Trace lanes (phase 3: every message, not only requests)
Run with `OCTOPUS_TRACE=<file>` (docs/Trace.md) and convert with
`convert.py <newLogger> --trace <file>`: an `occupancy.parquet` appears next to `octoviz.parquet`
and the run shows `trace: true` in `/api/runs`. Switch **lanes** to *trace (all traffic)*:

| lane | what a segment is |
|---|---|
| request bus | every GetS / GetM / PutM broadcast (grant → transmitted) |
| service bus (INV) | back‑invalidations the LLC sends on evictions |
| LLC array port | every port claim: response reads, write‑back / supply reads, fill and write‑back writes; **dashed outline = cut‑in** (admitted while the port was busy: MSHR/PWB‑resident line, see docs/Trace.md) |
| mem bus → DRAM / → LLC | memory reads and LLC evictions / DRAM fills (the point‑to‑point bus is full duplex) |
| DRAM | reads (accept → data back) and write accepts; banks overlap, so segments are packed into sub‑rows |
| response bus | LLC responses, cache‑to‑cache supplies (immediate and deferred), write‑backs |

Colour **by kind** (legend under the lanes), by core, or by class (requests only). Hover any segment
for its kind, message id, owner and cycles; click it to inspect the request with that id (an
eviction / LLC‑generated id has no request row — the status line says so). With a request selected,
the magenta wait windows and the white "ahead" outlines now include the non‑request occupants,
and the inspector lists them (*trace: ahead of it on …*) together with every trace interval of the
selected id (its queue residency, array claims, transfers, the supply or fill that served it).
Wide windows show utilisation per lane with the non‑request share (fills, write‑backs, INV,
memory traffic) overlaid in purple. The trace lanes are shifted onto the Logger's time base
(bus events −1) so they line up with the request rows; `occupancy.py --global-clock` keeps the
physical stamps.

## Coherence transition table (per line)
The full‑width panel at the bottom lists every event a controller's FSM processed on one 64‑B
line, in cycle order, from `fsm.parquet` (the trace's FSM records, docs/Trace.md): columns
`cycle | event (message kind) | message id + owner | L1‑0 | L1‑1 | L1‑2 | L1‑3 | LLC`. The acting
agent's cell shows **old → new** (or *stays* for a self‑transition / stall); the other cells carry
the state each agent held at that moment, so one row is the line's whole state vector after that
event. The line follows the address filter (masked to the line), or the clicked request's line;
rows caused by the selected request are highlighted and scrolled into view; clicking a row moves
the timeline to that cycle. *current window only* limits it to the visible cycles (a hot SPLASH
line can have millions of events; the panel shows the first 3 000 and says so). Setting an
address filter also restricts the trace lanes to that line.

## Next
1. DRAM per bank/channel (MCsim bank id in the trace).
2. Diff mode: two runs (e.g. PCC‑perfect vs real) on the same window.
3. FSM‑state overlay for a line from the dprint trace.
