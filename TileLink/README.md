# TileLink simulator component

A cycle-level, beat-accurate simulator of the TileLink 1.9.3 protocol (TL-UL, TL-UH, TL-C), written
against `docs/TileLink_Plan.md`. It is a standalone C++17 library with no Octopus dependency
(namespace `octopus::tl`); Octopus adds it with `add_subdirectory(TileLink)`. This is step T1: the
library skeleton (types, encodings, parameters, messages, channels, links, kernel, config reader,
transcript, statistics), the `tlsim` driver and the TL-UNIT tests. Agents, the monitor and the
checkers come with T2 and later.

## API overview (include/tilelink/)

| header | class | role |
|---|---|---|
| Types.h | `Channel`, `Level` | the five channels (A < B < C < D < E) and the three conformance levels |
| Types.h | `OpA..OpE`, `Arith`, `Logic`, `Intent`, `Cap`, `Grow`, `Prune`, `Report` | opcode and param encodings (Tables 13, 23, 25, 27, 31) |
| Types.h | `opcodeName`, `hasData`, `isRequest`, `responseOf`, `legalOn`, `paramLegal`, `beatCount`, `aligned` | the encoding rules as pure functions |
| Types.h | `fatal`, `ConfigError` | the single fatal path ("TileLink: msg", exit 2) and the refused-configuration exception |
| Params.h | `LinkParams` | w, a, z, o, i and the level of a link; `validate()` with exact messages |
| Msg.h | `MsgHeader`, `Msg`, `MsgPtr`, `DataMode` | one message: constant control fields, per-beat mask/data/corrupt, simulator timestamps; `beats()`, `beatAddress(k)`, `deriveMask()` |
| Channel.h | `ChannelState`, `Beat`, `ChannelStats` | the wire of one channel of one link: offered beat, valid, ready, the burst lock, counters, observer hooks (`onOffer`, `onAccept`, `onViolation`) |
| Link.h | `Link` | master to slave: its parameters and the `ChannelState`s it has (A+D or A..E) |
| Agent.h | `Agent`, `Role`, `LinkRole` | the abstract agent: `capability()`, the three phase hooks `offer`/`accept`/`commit`, `reset`, `idle`, a seeded `rng()` |
| Agent.h | `NullMaster`, `NullSlave`, `AgentRegistry` | the trivial pair, and the type-name registry `tlsim` builds systems from |
| Kernel.h | `Kernel`, `KernelOptions` | owns agents and links; the three-phase cycle; reset; `build()` from a config; `run`, `runUntilIdle`; transcript and stats |
| Config.h | `Config`, `Param` | reader of the Octopus CSV parameter format (`name(type),value`, `name[k].key`, wildcards, `Extends`, `-p` overrides) |
| Transcript.h | `Transcript` | one line per accepted beat: `cycle link ch opcode param size source sink address beat mask data corrupt denied` |
| Stats.h | `Stats`, `LatencyStats` | TLStats.csv: per-channel beats/busy/stall, per-opcode latencies, outstanding-ID histogram |

Timing contract (plan §3.2): offer, then accept, then commit, once per cycle; an offer at cycle t is
computed from state at the start of t, so there is no combinational pass-through and no same-cycle
response. Burst-lock and reset violations go to `ChannelState::onViolation` (default: `fatal`).

## Build and test

    cmake -S TileLink -B build-tl && cmake --build build-tl -j && ctest --test-dir build-tl --output-on-failure

## Run

    build-tl/tlsim -c TileLink/test/unit/null_system.csv -o out --trace -p cycles(i)=200

writes `out/TLStats.csv` and `out/transcript.txt`. Flags match `Octopus_Simulator`: `-c <config.csv>`,
`-o <dir>`, `--trace`, `-p name(type)=value`.
