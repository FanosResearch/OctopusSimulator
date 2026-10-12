# Adding a Coherence Protocol in Octopus

> A worked example: implementing **MI** (Modified/Invalid), the simplest coherence
> protocol, **without writing or compiling any C++** — the protocol is a CSV
> finite-state machine. Files:
> [`Protocols_FSM/MI_splitBus_snooping.csv`](../Protocols_FSM/MI_splitBus_snooping.csv),
> toy trace [`BMs/eembc-traces/mi_pingpong/`](../BMs/eembc-traces/mi_pingpong).

In Octopus a cache-coherence protocol **is** a spreadsheet: rows are states (stable
and transient), columns are coherence events, and each cell is `action(s)/next-state`.
The [`FSMReader`](../header/FSMReader.h) parses it at startup, and a generic
protocol handler executes whatever actions the table returns. So a new protocol is a
new CSV — see [Architecture §5](Architecture.md).

---

## 1. The protocol: MI

MI is the simplest coherence protocol: a cache line is either **Invalid** or
**Modified**. There is no *Shared* state, so **every access — even a load — takes
the line exclusive**. It is MSI with the `S` state (and its transients) removed and
`Load` behaving like `Store`:

| | MSI | MI |
|---|---|---|
| stable states | I, S, M | **I, M** |
| total states (incl. transient) | 22 | **9** |
| a load issues | `GetS` (shared) | **`GetM` (exclusive)** |
| shared reads | hit in `S` | **fault** (MI has no sharing) |

Because MI uses only a **subset** of MSI's events and actions, it runs on the
existing `SNOOP_MSI` protocol handler and the existing `MSI_LLC` — **no code
changes**. The entire protocol is the one CSV file.

Excerpt (`MI_splitBus_snooping.csv`):

```
State, stable, isDataValid, Load,        Store,       ... , OwnData,           Invalidation
I,     1,      0,           GetM/IM_ad,  GetM/IM_ad,  ... , Fault/,            
M,     1,      1,           Hit/,        Hit/,        ... , Fault/,            Data2Req/I
...
```

## 2. Select it — one line, no rebuild

Point the L1 cache at the MI table. Either edit the system config:

```diff
- cache_controller[*].fsm_filename(s),MSI_splitBus_snooping,,,,,
+ cache_controller[*].fsm_filename(s),MI_splitBus_snooping,,,,,
```

…or override it on the command line (no file edit):

```bash
./build/Octopus_Simulator -s MultiCoreSystem \
  -p "workload_path(s)=BMs/eembc-traces/mi_pingpong/" \
  -p "cache_controller[*].fsm_filename(s)=MI_splitBus_snooping"
```

`protocol_type` stays `SNOOP_MSI` and the whole LLC row is unchanged — MI reuses them.

## 3. Run it

The `mi_pingpong` toy has cores 0 and 1 repeatedly **read the same line** `0x1000`
(cores 2–3 idle). MI runs it **fault-free, all four cores complete** — the FSM has
no undefined `(state,event)` cells for this workload.

> **One-command demo.** `bash demo_protocol.sh` runs the toy under MESI and MI and
> prints both the cost and the coherence trace shown below:
> ```
> == Cost of the two protocols on the SAME read-sharing workload ==
>    MESI  (reads SHARE)     -> finish = 332 cycles
>    MI    (reads PING-PONG) -> finish = 482 cycles
> == Coherence trace on the shared line 0x1000 (a load's transition) ==
>    MI:   I --Load--> IM_ad   (GetM: the read goes exclusive -> line ping-pongs)
>    MESI: I --Load--> IS_ad   (GetS: the read is shared -> cores share)
> ```

## 4. See the difference: MI vs MESI

Under **MESI** the two cores *share* the line; under **MI** they can't, so the line
**ping-pongs** exclusively between them. Octopus makes this visible two ways.

**Cost (the Logger).** Same read-sharing workload, one config apart:

| protocol | a load does | finish |
|---|---|---|
| MESI | `GetS` — cores share | **332 cyc** |
| MI | `GetM` — line ping-pongs | **482 cyc (≈45% slower)** |

**Behavior (the Debugger).** An address-filtered coherence trace on the shared line
shows the FSM transitions directly:

```
# MI  (I -> M path: every read is exclusive)      # MESI (I -> S path: cores share)
CacheController0  I --Load--> IM_ad                CacheControllerExclusive0  I --Load--> IS_ad
CacheController1  I --Load--> IM_ad                CacheControllerExclusive1  I --Load--> IS_ad
CacheController0  IM_ad --Own_GetM--> IM_d         CacheControllerExclusive0  IS_ad --Own_GetS--> IS_d
CacheController0  IM_d --Other_GetM--> IM_dI       CacheControllerExclusive0  IS_d --Other_GetS--> IES_d
CacheController1  IM_ad --Own_GetM--> IM_d         CacheControllerExclusive1  IS_ad --Own_GetS--> IS_d
#  ^ line leaves C0, migrates to C1 (ping-pong)    #  ^ both cores end up sharing
```

Enable that trace by turning on the L1 debugger, filtered to the block (see
[Debugger.md](Debugger.md)):

```bash
  -p "cache_controller[*].dprint.enable(i)=1" \
  -p "cache_controller[*].dprint.target(s)=coh.csv" \
  -p "cache_controller[*].dprint.print_preamble(i)=1" \
  -p "cache_controller[*].dprint.print_name(i)=1" \
  -p "cache_controller[*].dprint.print_clk(i)=1" \
  -p "cache_controller[*].dprint.print_addr(i)=1"
```

## 5. What it took

- **1 new file** — a 9-row CSV FSM.
- **1 config line** (or one `-p` flag) to select it.
- **0 lines of C++, 0 recompiles.**

That is the whole point: the hardest component to change in a memory-system
simulator — the coherence protocol — is, in Octopus, an editable table. Adding a
richer protocol (e.g. a new stable state, or a time-based variant) is the same
process with more rows.
