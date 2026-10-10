# Line state and line data

A controller handles a message by running one row of its FSM: the row changes
the line's **state** and runs **actions**, some of which read or write the
line's **data** in the data array. When the data array has a latency, the state
change and the data access can happen at different cycles, and another message
can see one without the other. This note defines the two data-array models and
the three configurations they allow, and shows in which one state and data can
come apart.

## The two data-array models

The data array of a cache (`m_data_handler`) has an access latency *L*
(`m_data_access_latency`, cycles). How a controller pays it is the array model
(`m_data_array_pipelined`):

| | occupancy model (`0`, default) | pipelined model (`1`) |
| --- | --- | --- |
| an access | starts if the array is free, runs at once, then closes the array for *L* cycles | is admitted to the pipeline and completes *L* cycles later |
| the requester pays | 0 cycles (only the wait for a free array) | *L* cycles |
| throughput | one access per *L* cycles | `m_data_array_ports` accesses admitted per cycle |
| what waits for the array | the byte phase, or the whole message with `line_interlock=1` (below) | always the whole message |

In both models, an access that cannot start at once goes on the controller's
**array-access list**. Each cycle the list is served by the controller's data
arbiter (`arbiter_type`, electing by requesting core) or, without one, oldest
first: one access when the array opens (occupancy), up to `ports` admissions
(pipelined).

Two rules hold in both models:

- **With *L* = 0 nothing waits:** every row changes state and data in the same
  cycle. (The pipelined model is only active for *L* > 0.)
- **Lines in the MSHR or the write-back buffer are not in the array:** accesses
  to them run in place, without waiting for the array.

## The three configurations

The array model and `line_interlock` (CacheController) give three
configurations. They differ in what waits when the array is busy:

| configuration | set by | what waits for the array | state and data |
| --- | --- | --- | --- |
| A. occupancy, no interlock (default) | `m_data_array_pipelined=0`, `line_interlock=0` | the row's byte phase only | the row runs at pop: the state changes now, the bytes later |
| B. occupancy with interlock | `m_data_array_pipelined=0`, `line_interlock=1` | the whole message, when the array is busy at pop | change together |
| C. pipelined | `m_data_array_pipelined=1` (turns `line_interlock` on) | the whole message, always | change together |

A message is deferred whole when its row touches the array. The protocol
reports those rows (`needsDataArray`): `Hit`, `Data2Req` and `Data2Both` in the
L1 snoop protocols; `SendData`, `SendExeclusiveData` and `SaveData` in the LLC
protocols. In C, a message that carries bytes from below or from a peer (a fill)
is an array write as well. A deferred message leaves the processing queue and
goes on the array-access list as a unit; its row runs when its array access
runs.

Whole-message deferral is never used without the interlock: once a message has
left the queue, the interlock is what stops a younger message to the same line
from acting on the line before it. That is why B needs `line_interlock=1` and C
turns it on (`the pipelined data array needs line_interlock; enabling it`).

## The line interlock

While a line has an entry on the array-access list, every queued message to
that line stays in the processing queue, in place and not ready, until the entry
is gone. Same-line traffic therefore runs in arrival order against the state the
older access leaves, as in a hardware pipeline that holds a line from tag to
data. Holding in place, rather than popping and parking, keeps each message's
queue position, so bus order and the per-line FCFS gate still apply.

## Where state and data come apart: configuration A

In A, a row whose byte phase waits has already changed the line's state. Two
situations follow at an L1 with *L* > 0:

- **A load hit overtaken by an invalidation.** Core A loads X, which is in S; the
  row is a hit and its read waits for the array. Core B's GetM is snooped at A;
  the row S → I needs no data, runs at once and invalidates X. A's read then runs
  against a line that is I (under gem5, bytes B is about to overwrite).
- **A snoop answer whose own state change dropped the line.** A snoop answered
  with data (`Data2Req/I`, `Data2Both/S`) reads the line for the write-back, then
  changes state. The read waits, the state change runs, and the line is
  invalidated or replaced before the read: the write-back sends stale bytes or
  none.

Neither happens in B or C: the hit's row and the snoop answer's row run together
with their access, and the invalidating snoop waits behind the hit in the queue.

At the LLC, A is safe in the standalone presets: no LLC row invalidates or
replaces what an older waiting byte phase still needs, and traces carry no
bytes.

## Rules that hold in every configuration

- **Bus order inside a queue.** A bus message can wait in a controller's queue:
  the per-line FCFS gate holds a back-invalidation behind the core's own older
  request to its line, and the interlock holds any message to a line with a
  pending access. `FRFCFS_Buffer::pushFrontOrdered` puts a bus message ahead of
  the core's own requests but behind older bus messages, so a younger snoop
  cannot overtake an older one to the same line.
- **A line leaving the array.** An eviction moves the victim into the write-back
  buffer, and the WriteBack's state change releases the buffered copy at once.
  Every access still waiting on the victim completes before its bytes move
  (`pipelineFlushLine`); after that, accesses to the line run in place in the
  buffer.

## What real data adds (the gem5 integration)

Standalone runs drive addresses only: if state and data disagreed for a few
cycles, only timing would change. Under gem5 the bytes are real:

- A load must return the latest store in coherence order. The two situations of
  configuration A return old or missing bytes, which the program sees.
- A core's younger access to a line with its own older store in flight must see
  that store. The gem5 bridge holds such an access back behind the older
  overlapping write (`gem5/octopus.cc`).
- LL/SC depends on the invalidation reaching the core at the right point; a
  snoop that runs early (as in configuration A) or late changes which SC
  succeeds.

## Settings

| setting | default | gem5 preset | effect |
| --- | --- | --- | --- |
| `m_data_access_latency` (CacheDataHandler) | 0 (L1), 10 (LLC) | 0 (L1), 10 (LLC) | the array latency *L* |
| `m_data_array_pipelined` (CacheDataHandler) | 0 | 1 (LLC) | 0 = occupancy model, 1 = pipelined model; pipelined turns the interlock on |
| `m_data_array_ports` (CacheDataHandler) | 1 | 1 | pipelined model only: accesses admitted per cycle |
| `line_interlock` (CacheController) | 0 | 1 (L1 and LLC) | hold messages to a line with a pending array access; enables whole-message deferral in the occupancy model |

What the presets use:

| preset | L1 | LLC |
| --- | --- | --- |
| standalone (`MultiCoreSystem`, `_Snoop`, `_Directory`, `_Mesh`) | *L* = 0: nothing waits | A, *L* = 10 |
| gem5 (`MultiCoreSystem_gem5`) | *L* = 0: nothing waits | C, *L* = 10, 1 port |

Use A only for trace-driven runs with an L1 latency of 0. With real data, or an
L1 with *L* > 0, use B or C.
