# Octopus real-time tracking demo: context notes

Companion file to `octopus-tracking-demo.html`. Written so a later session can pick the demo
up without re-deriving anything.

## What the demo is

A single self-contained HTML page. No build step, no dependencies except Google Fonts, which
falls back to system fonts offline. Open it in a browser or serve it from any static host.

It shows a pen plotter tracing a reference path, driven by a 200 Hz control task running on a
simulated four-core machine with a shared last-level cache and one memory controller. The point
of the demo is that the trajectory is a *pure function of when control jobs finish*, so timing
interference becomes something you can see rather than something you read off a table.

Three acts, selectable by button, keys 1/2/3, or an auto-play sequence:

1. **Alone.** Core 0 owns the memory system. Jobs finish in about 2.4 ms inside a 5 ms period.
   The pen sits on the reference path.
2. **Contention.** Three background cores stream memory. They evict the control task's lines from
   the shared cache and queue ahead of it at the controller. Per-access latency goes from roughly
   19 ns to roughly 190 ns, jobs run about 13 ms, so setpoints arrive late and stale. The pen lags,
   cuts chords across curves, and overshoots corners.
3. **Protected.** Reserved cache ways for core 0 plus a bandwidth budget on the background cores.
   Latency becomes bounded and analysable, jobs fit inside 5 ms again, the pen converges back
   onto the line.

Ink from earlier acts stays on the page with an age fade, so the clean arc is still visible under
the degraded one and the recovery reads clearly.

## What is modelled, and how

Everything lives in the `P` (parameters) and `S` (state) objects near the top of the script.

Per control job, sampled at release:

```
pressure = min(1, cores * intensity / 3)              # 0 when running alone
rho      = 0.08 + 0.72 * pressure                     # 0.08 + 0.30 * pressure when protected
L_dram   = 55 / (1 - rho)                             # ns, unloaded base 55 ns
miss     = 0.10 + 0.58 * pressure                     # 0.12 when protected (way reservation)
L        = (1 - miss) * 14 + miss * L_dram            # 14 ns LLC hit
C        = 1.20 + 64 * L / 1000  (+ 0.25 if protected)  # ms
```

Job release is `max(previous_finish, next_period_boundary)`, so an overrun pushes the next release
out and the deadline miss count rises. The controller samples the reference at release and applies
the setpoint at finish, so the commanded point is stale by exactly `C`. The actuator chases the
last setpoint at a speed limit of 1.55x path speed. That single rule produces all the visible
behaviour: lag under long `C`, straight chords when updates are sparse, corner overshoot, and
dwell when the pen reaches a stale setpoint before the next one arrives.

Other parameters worth knowing: 5.0 ms control period, 64 memory accesses per job, 500 ms of
simulated time per lap of the path, 60 mm per normalised unit for the error readout. Playback is
slowed (default 12x) so that one control period is visible on screen.

## What is invented and should be replaced

The numbers above are a queueing model, not Octopus output. They were chosen to make the three
acts legible, not to match any measured configuration. Two things in particular are placeholders:

- **The protection mechanism.** The schematic and captions say way reservation plus bandwidth
  budgeting plus predictable arbitration. If Octopus's real-time mode does something else (TDM
  slots, criticality-aware arbitration, per-core latency budgets), relabel `CAPTIONS`, `llcNote`,
  `mcNote`, and the `.gate` and `.part` elements in the SVG.
- **The latency model.** Replace `sampleJob()` with replayed values as soon as traces exist.

## Feeding it from Octopus

The visualization needs one record per control job and nothing else:

```json
{"job":  1421,
 "release_cycle": 2842000,
 "finish_cycle":  2846800,
 "n_accesses": 64,
 "mean_lat_ns": 191.4,
 "max_lat_ns":  274.0,
 "llc_misses": 43,
 "deadline_miss": true}
```

Pen position, tracking error, chord cutting and the error callout are all derived in the browser
from `finish_cycle`. The control task's own arithmetic does not need instrumenting.

Three wiring options, in increasing order of coupling:

- **Replay.** Octopus writes JSONL, the page loads the file. Zero coupling, never stalls, safest
  for a conference floor. Add a file-drop handler to the existing page and it works with the
  hosted copy too.
- **Tail and stream.** A small Python bridge tails the same JSONL and pushes over WebSocket. The
  page buffers a few hundred ms of simulated time and plays behind the write head, which is what
  keeps it smooth when the simulator stalls. This is the "watch it while it simulates" version.
  Must be served locally, since a hosted page cannot reach localhost.
- **In-process.** Octopus pushes to a socket at each job completion. Tightest, but it gives the
  simulator a network dependency to debug on demo day.

Note on expectations: a cycle-level run is orders of magnitude slower than wall clock, so the pen
advances in simulated time at a chosen playback rate. It is live as the simulation progresses,
not live at the speed of the modelled machine.

## Open items

- Confirm what Octopus already logs per control job, so the schema above matches rather than
  competes with it.
- Decide the demo venue. A conference floor argues for replay from a canned trace; a lab or class
  demo argues for the streaming build.
- Possible additions: a second protected task to show budget sharing, a per-core bandwidth meter,
  and an export button that dumps the run as CSV for a paper figure.
