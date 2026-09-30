# When line state and line data come apart

A controller handles a message by running one row of its FSM: the row changes
the line's **state** and runs **actions**, some of which read or write the
line's **data** in the data array. As long as both happen in the same cycle,
nobody can see one without the other. This note lists the situations where a
data-array latency separates them, what goes wrong in each, whether the
presets on this branch can reach it, and what handles it.

## Why the presets on this branch do not show it

| setting | L1 | LLC |
| --- | --- | --- |
| `m_data_access_latency` | 0 | 10 |
| array model | none needed | occupancy (upstream) |
| `line_interlock` | 0 | 0 |

- **L1 latency 0.** An L1 never parks anything, so every L1 row changes state
  and data together.
- **LLC occupancy model.** The row runs at pop (state now) and only its byte
  phase waits for the array (data later). This is a tag-then-data shape
  without a lock on the line in between. It is correct here because of how
  the LLC tables are written, not by construction: no LLC row invalidates or
  replaces what an older row's parked byte phase still needs, and parked byte
  phases are served first-in first-out.
- **Standalone runs carry no data.** A trace only drives addresses. If state
  and data did disagree for a few cycles, the only visible effect would be on
  timing; no check compares the bytes.

Once real data flows (the gem5 integration: loads return bytes, LL/SC and
programs check them), or an L1 gets a latency, or the LLC is pipelined, the
situations below become reachable.

## The situations

### 1. A load hit is overtaken by a remote invalidation (L1 with latency)

Setting: L1 `m_data_access_latency` > 0, occupancy model.

1. Core A loads X, which is in S. The row is a hit; the array is busy, so the
   byte phase parks.
2. Core B's GetM for X is snooped at A. The row S → I needs no data, so it
   runs at once and invalidates X.
3. A's parked read runs against a line that is now I.

Upstream: the load reads an invalidated line (in gem5, possibly bytes that B
is about to overwrite). With whole-message deferral (the hit's row runs when
its array access runs) the load re-runs as a miss: coherent, but not what
hardware does. With `line_interlock=1` the snoop waits in the queue until the
hit's access has completed, as a real pipeline holds a line from tag to data.

### 2. A snoop supplies a line that its own state change already dropped (L1 with latency)

Setting: L1 latency > 0, occupancy model.

A snoop answered with data (rows `Data2Req/I`, `Data2Both/S`) expands to a
write-back that reads the line, then a state update. With the array busy, the
read parks and the state update runs; the line is invalidated or replaced
before the parked read runs. Upstream: the write-back copies from a NULL line
(crash) or sends stale bytes. Handled by deferring the whole message: the row,
and with it the state change, runs when the array access runs.

### 3. Parked accesses without an arbiter are never served (L1 with latency)

Setting: L1 latency > 0; L1s have no data-access arbiter.

Upstream served parked byte phases only through the arbiter, so at an L1 they
waited forever: the second store of a same-line burst hung the core. The
array-access list is now served oldest-first when there is no arbiter.

### 4. A younger message acts on a line whose older access is still waiting

Setting: any array latency, whole-message deferral on, `line_interlock=0`.

Deferring a whole message keeps its state change with its data, but the
message has left the queue. A younger message to the same line (a snoop, a
request, an invalidation) can now start before it, against a state the older
message was about to change. This is the situation behind the one fault seen
while porting to this branch: with the LLC deferring its array-touching rows
(`SendData`, `SendExeclusiveData`, `SaveData`) and no hold, the Snoop preset
stopped with `MSIProtocol: Fault Transaction is detected`; with the hold it
ran clean.

Handled by `line_interlock=1`: while a line has an entry on the array-access
list, every queued message to it stays in place, not ready, until the entry is
gone. Holding in place (rather than popping and parking) keeps each message's
position, so bus order and the per-line FCFS gate still apply. This is why
whole-message deferral is tied to the interlock: with `line_interlock=0` the
occupancy model keeps upstream's handling, and the pipelined model turns the
interlock on.

### 5. A younger bus message to a line jumps ahead of an older one that waits

Setting: any configuration where a message from the bus can wait in an L1's
queue. On this branch that includes the default presets: the per-line FCFS
gate (7c9fa4bd) holds a back-invalidation behind the core's own older request
to its line. (Snoops from the bus, GetS/GetM of other cores, are not demand
requests at the L1 and are not gated.) With `line_interlock=1` any bus message
can also wait while its line has an array access pending.

Upstream inserted every bus message at the very front of the L1's queue. That
was equivalent to bus order only while a bus message never waited. If one
waits, the next bus message to the same line is inserted in front of it and,
having nothing older ahead of it, can run first: a snoop overtakes the
back-invalidation that preceded it on the bus, or, under the interlock, one
snoop overtakes another. In the second case the owner answers the wrong
requester; the older GetM finds the line in S, no data is sent, and its
requester waits forever (seen on gem5_integration before the fix). Handled by
`FRFCFS_Buffer::pushFrontOrdered`: a bus message goes ahead of the core's own
requests but behind older bus messages. It is unconditional; the lab presets
are unchanged by it.

### 6. A parked read of a line in the write-back buffer loses its line (pipelined model)

Setting: `m_data_array_pipelined=1`.

An eviction moves the victim into the write-back buffer; its WriteBack reads
the buffered copy and the state change that follows (to N) releases the
entry at once. A read of that line parked on the array pipeline found nothing
when it ran. Lines in the MSHR or the write-back buffer are not in the array,
so accesses to them now bypass the pipeline and run in place.

### 7. Directory rows that assumed the LLC answers at pop

Setting: directory protocols, once the LLC's outgoing traffic can be
reordered (deferral and hold, or a NoC that delays some links).

- A PutS was classified as the last one by the size of the sharer list alone.
  Arriving after the invalidation that removed its sender, it drove the line
  to I with a live sharer, and the next GetM was answered with a stale ack
  count.
- `MSI_directory.csv` rows `IM_aI` / `IM_aSI` handed the line on at the last
  ack without performing the requester's own store.

Both are fixed on this branch by d9b4f6b4; gem5_integration found the same two
through the deferral path.

### 8. What real data adds (the gem5 integration)

With gem5, the bytes matter as well as the timing:

- A load must return the value of the latest store in coherence order.
  Situations 1, 2 and 4 return old or missing bytes, which a program sees.
- A core's own younger access to a line with an older store still in flight
  must see that store. The gem5 bridge holds such an access back behind the
  older overlapping write (hold-back in `gem5/octopus.cc`).
- LL/SC depends on the invalidation reaching the core at the right point;
  a snoop that runs early (situation 1) or late changes which SC succeeds.

## Settings

| setting | default | gem5 preset | effect |
| --- | --- | --- | --- |
| `line_interlock` (CacheController) | 0 | 1 (L1 and LLC) | hold messages to a line with an array access pending; enables whole-message deferral in the occupancy model |
| `m_data_array_pipelined` (CacheDataHandler) | 0 | 1 (LLC) | accesses pay the latency and one is admitted per port per cycle; always defers, so it turns `line_interlock` on |
| `m_data_array_ports` | 1 | 1 | pipelined model only |

Rule of thumb: an array latency with real data needs `line_interlock=1`. The
occupancy model with `line_interlock=0` is upstream's model and is only safe
for trace-driven runs with an L1 latency of 0.
