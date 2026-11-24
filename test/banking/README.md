# LLC banking and partitioning tests

Tests for the multi-bank LLC, the partitioning framework and the MCsim fixes
([docs/LLC_Banking_and_Partitioning_Design.md](../../docs/LLC_Banking_and_Partitioning_Design.md)).
Run them from the repository root after building `build/Octopus_Simulator`. The EEMBC traces are in
`BMs/eembc-traces`; `./get_benchmarks.sh` fetches them, and the SPLASH traces too.

Every script exits non-zero on a failure. A run passes only when every core's `LatencyReport` has its
end-of-run `Average` footer: the simulator can stop early on a protocol Fault with exit code 0.

| Script | Checks | Time (20 cores) |
|---|---|---|
| `regress.sh ref <bin> <dir>` / `regress.sh cmp <refdir> <dir>` | byte identity of Summary and LatencyReports over 81 runs (snoop and directory families, bus arbiters, MCsim, perfect LLC, way partitioning) against a reference build | 20 s |
| `banked.sh <dir> [banks...]` | EEMBC on 2 and 4 banks (default), line and page interleave, all six protocol families: 240 runs complete | 50 s |
| `PROPS=1 banked.sh <dir>` | the same, plus the banking properties of design s8.3 on every run (`check_bank_trace.py`) | 70 s |
| `splash_banked.sh <dir> [splash_dir]` | the SPLASH subset of s8.1 (ocean, water_nsquared, water_spatial, fmm, lu_non_contig, cholesky): snooping MOESI on 2 banks, both interleaves, with the properties | 4 min |
| `partition.sh [dir]` | way, colour and bank partitions, the page placer, `strict`, `Partitions.csv` | 35 s |
| `subbanks.sh [dir]` | per-sub-bank data-array occupancy against the formula | < 1 s |
| `mcsim_wrap.sh [bin] [dir]` | MCsim keeps issuing DRAM commands past device cycle 2^32 | 10 s |
| `unit/addressmap_test.cpp` | AddressMap round trip, home map, colours (build line at the top of the file) | < 1 s |
| `unit/partition_test.cpp` | contract tests run on every registered partition scheme | < 1 s |

`make_banked.py` derives an N-bank preset from a one-LLC preset; `banked.sh` calls it.

## Bank properties

`check_bank_trace.py <trace.bin> --banks 10,11 --shift 0` reads a run's raw trace (`--trace`) with the
Python standard library only. It checks three things:

- **home**: every record a bank writes is for a line that bank is home for;
- **single**: no message is handled by more than one bank;
- **reached**: every request on the request bus reaches exactly one bank, its home.

A request sent in the last 1000 cycles of the trace counts as in flight, not as a failure. A bank with
no records is noted, not failed (`mi_pingpong` has 4 requests in total).

## banked.sh settings

| Variable | Effect |
|---|---|
| `ONLY=<regex>` | protocol families to run (`snoop-msi`, `snoop-mesi`, `snoop-moesi`, `dir-msi`, `dir-mesi`, `dir-moesi`) |
| `ARB=<arbiter>` | bus arbiter. Use the class names `FCFSArbiter`, `RRArbiter`, `TDMArbiter`: a wrong name ends every run early |
| `PROPS=1` | trace every run, check the properties, delete the trace (about 100 MB per EEMBC run, 3 GB per SPLASH run) |
| `BMDIR`, `BENCHES` | other trace directory and benchmark list |
| `EXTRA="-p ..."` | settings added to every run |
| `JOBS`, `CAP` | parallel runs (16) and per-run timeout in seconds (1800) |
| `DIR_MESI_L1`, `DIR_MESI_LLC`, `DIR_MOESI_L1`, `DIR_MOESI_LLC` | directory FSM tables |

The full check after a protocol change is `banked.sh` with `PROPS=1` under each of the three arbiters,
plus `regress.sh cmp`. FCFS alone hides timing races that RR and TDM reach.

## Overriding a one-LLC preset

With one LLC the key is `llc_controller.<key>`. `llc_controller[*].<key>` matches only the banks of a
multi-bank preset, so on a one-LLC preset it silently leaves the LLC on the preset's protocol while the
L1s change, and the run hangs.
