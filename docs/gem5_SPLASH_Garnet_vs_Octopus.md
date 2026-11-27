# gem5 Ruby/Garnet against gem5 + Octopus on SPLASH-3

Status: 2026-11-27, pilot done (3 programs, small inputs, 16 cores, 4 configurations). Octopus
branch `network` at 2c37cf53 plus the uncommitted files listed in §8. gem5 v25.1.0.1 (`c8222cc`).
Platform: Linux on aarch64, 20 cores shared with other jobs.

Contents

1. Goal
2. Builds
3. Matched configuration
4. Remaining mismatches
5. How to run
6. Validation
7. Pilot results
8. Files
9. Bugs found
10. How to read the results
11. Full-matrix estimate

---

## 1. Goal

Compare two complete gem5 systems that differ only in the memory hierarchy below the cores:

- **Ruby**: gem5's Ruby `MESI_Two_Level` protocol (private L1I/L1D, shared banked L2 holding the
  directory) on a 4x4 mesh, with Garnet 3.0 (`garnet`) or the Ruby SimpleNetwork (`simple`);
- **Octopus**: Octopus's directory MESI (`MESI_directory_v2` / `MESI_LLC_directory_v2`) on its
  flit-level 4x4 mesh NoC, driven through the gem5 bridge (`octopus`);
- **classic**: gem5 classic caches on a crossbar, as a reference without a network (`classic`).

All four run the same static aarch64 SPLASH-3 binaries (`splash-gem5`, ROI = `m5_work_begin` /
`m5_work_end`) in gem5 SE mode on the same 16 O3 cores at 3 GHz. The network parameters follow the
Garnet synthetic validation (`docs/NoC_Garnet_Validation.md`), so a difference in the SPLASH results
comes from the protocols, the endpoints and the network interfaces, not from a different router.

## 2. Builds

| side | binary | build |
|---|---|---|
| Ruby (garnet, simple) | `/home/mohassan/Octopus_transfer/gem5-ruby/build/ARM_MESI2/gem5.opt` | worktree of `c8222cc`, see below |
| Octopus, classic | `/home/mohassan/Octopus_transfer/gem5-noc/build/ARM/gem5.opt` | gem5 with this tree as EXTRAS (unchanged) |

```
git -C /home/mohassan/Octopus_transfer/gem5 worktree add --detach /home/mohassan/Octopus_transfer/gem5-ruby c8222cc
cd /home/mohassan/Octopus_transfer/gem5-ruby
printf 'BUILD_ISA=y\nUSE_ARM_ISA=y\nRUBY=y\nPROTOCOL="MESI_Two_Level"\nRUBY_PROTOCOL_MESI_Two_Level=y\n' > build_opts/ARM_MESI_Two_Level
git apply <octopus>/test/gem5_splash/gem5-garnet-functional-read.patch
scons defconfig build/ARM_MESI2 build_opts/ARM_MESI_Two_Level
nice -n 19 scons build/ARM_MESI2/gem5.opt -j8
```

The first build takes about 70 minutes at `-j8` on this machine. The patch is needed (§9.5): without
it Garnet stops with `fatal: Functional read not implemented` as soon as an SE syscall (`futex`)
reads a line that is in flight. The Garnet validation patch (`test/network/garnet/gem5-validation.patch`)
is not applied: its extra statistic is not needed here.

## 3. Matched configuration

| item | Ruby `MESI_Two_Level` (garnet / simple) | Octopus (`MultiCoreSystem_gem5_mesh_ruby`) | classic (reference) |
|---|---|---|---|
| cores | 16 O3 (stdlib `SimpleProcessor`), 3 GHz, SE, one thread per core | same | same |
| L1I, L1D | 32 KiB 8-way each, 64 B lines, LRU | 32 KiB 8-way each, 64 B lines, LRU | 32 KiB 8-way each, LRU |
| L1 hit | `mandatory_queue_latency` 2: 2 cycles (Sequencer hit histogram mean 2.000) | 2 cycles (LatencyReport L1 Stall 1 + L1 Access 1) | tag 3, data 3 |
| L1 outstanding misses | Sequencer `max_outstanding_requests` 16 (I and D together) | `num_mshr` 16 per L1 (I and D separately) | 16 MSHRs per L1 |
| shared L2 / LLC | 16 banks x 512 KiB 8-way (8 MiB), line-interleaved (bank = line address bits 0-3), LRU | 16 banks x 512 KiB 8-way (8 MiB, set = address bits 10-19, §9.1), `home_interleave_shift 0`, LRU | one 8 MiB 8-way L2 (the same sets as 16 line-interleaved banks of 1024 sets) |
| L2 data access | `l2_response_latency` 11 | `m_data_access_latency` 10, pipelined, 1 port (LatencyReport L2 Access 11) | tag/data/response 10 |
| directory | in the L2 tags (inclusive L2) | in the LLC bank (inclusive LLC) | snooping crossbar |
| other latencies | `l1_request_latency` 1, `l1_response_latency` 1, `l2_request_latency` 1, `to_l1/to_l2_latency` 1 | the controllers' own pipelines | |
| tiles | router t: L1 controller t, L2 bank t | router t: L1I t, L1D 16+t, bank 64+t, memory port 80+t | no network |
| memory controllers | 2 Directory controllers, at routers 4 and 11 | 2 MCsim controllers 100 and 101, at routers 4 and 11 (`memory_on_network 1`) | on the memory bus |
| DRAM | gem5 DDR4 timed as MCsim (`--dram mcsim_match`, §3.1), 2 channels interleaved by 64 B | MCsim DDR4-2400U, 8Gb x8, 1 rank, FR-FCFS, open page, per controller; `mem_interleave_shift 0` | as Ruby |
| topology, routing | 4x4 mesh, XY (Garnet `routing_algorithm 1`; SimpleNetwork link weights x 1, y 2) | 4x4 `grid2d`, XY | |
| router + link | Garnet router latency 1, link latency 1 | `router_latency 0`, `link_latency 2`, `credit_latency 2` (= Garnet 1 + 1, validation §2) | |
| flits | `ni_flit_size` 16 B; control 8 B = 1 flit, data 72 B = 5 flits | `flit_bytes 16`, `header_bytes 8`: 1 and 5 flits | |
| VCs, buffers | Garnet 4 VCs per vnet, `buffers_per_data_vc` 4, `buffers_per_ctrl_vc` 4; SimpleNetwork: no VCs, unbounded buffers, 16 B/cycle per link | 1 VC per class, `vc_depth` 4 (§4) | |
| allocation | Garnet separable input-first, round robin | `sa_allocator input_first`, `arbiter_type RRArbiter` | |
| classes | 3 vnets | 3 classes (request, forward, response) | |
| network clock | core clock (RubySystem in the board clock domain) | core clock (`m_clk_period 100`) | |
| prefetchers | none | none | none (`--classic-no-prefetch`) |
| memory size | 8 GiB | 8 GiB (gem5 DDR4 stores the bytes, MCsim times them) | 8 GiB |

### 3.1 DRAM

Octopus times DRAM with MCsim; the gem5 DRAM behind the bridge only stores bytes (the bridge
performs every access atomically when Octopus commits it, `gem5/octopus.cc` `performData`). The
bridge cannot use gem5's DRAM timing today: there is no Octopus memory controller that hands
`K_MEM_READ` / `K_MEM_WRITE` to gem5 and waits for a callback (`octopus.cc` still carries a
commented-out `ExternalMem` stub in `handleResponse`). Building one is a few days of work: an
`ExternalMemory` controller in Octopus (the MCsimInterface pattern, with the gem5 callbacks in
place of MCsim's), a timing request port on the bridge to a second, data-less gem5 memory
(`null = True`, outside the address map, so timing writes cannot clobber the bytes the atomic path
keeps), and a bridge clock that keeps Octopus running while memory requests are outstanding.

The comparison therefore goes the other way: the gem5 DRAM of the Ruby and classic runs is set to
MCsim's timing (`gem5/configs/matched_dram.py`, `--dram mcsim_match`): `DDR4_2400_8x8` with one
rank per channel, the 2400U speed bin of MCsim (CL = tRCD = tRP = 18 tCK = 15 ns, tRAS 39 tCK,
tWTR_S/L 3/9 tCK), and no static frontend or backend latency. MCsim adds none: its unloaded
row-hit read in the Octopus LatencyReport is 53 to 55 core cycles, CL plus the burst. Stock gem5
DDR4-2400 (`--dram ddr4_2400`) is 17-17-17 at 14.16 ns, two ranks, and 10 + 10 ns of static
controller latency (about 60 core cycles more per access).

What stays different: the address mapping (gem5 `RoRaBaCoCh`, MCsim row, rank, bank group, bank,
column), the schedulers (both FR-FCFS, but gem5 has separate read and write queues of 32 / 64
entries with write draining, MCsim one queue with `WriteQueue=0`), the page policy (gem5
`open_adaptive`, MCsim open), refresh, and the queues in front of the controller (Ruby Directory
controller with `to_mem_ctrl_latency 1`; Octopus memory controllers that accept writes on arrival
and acknowledge them, spec §25.2).

## 4. Remaining mismatches

Protocol:

- **Directory placement**: both keep the directory with the shared cache (Ruby in the L2 tags,
  Octopus in the LLC bank); Ruby's `Directory` controller is only a memory front end, Octopus's
  memory controllers play that role.
- **Evictions**: MESI_Two_Level drops clean S lines silently and sends `PUTX` for E and M; Octopus
  notifies the home on every eviction (`PutS`, `PutE`, `PutM`) and waits for `PutAck`. Octopus's
  directory LLC writes back every eviction to memory, clean ones included; Ruby's L2 writes back
  dirty lines only. With an 8 MiB LLC and the small inputs there are few LLC evictions.
- **Unblocks**: MESI_Two_Level L1s send an `UNBLOCK` / `EXCLUSIVE_UNBLOCK` to the L2 after every
  exclusive or shared grant from the L2's busy state; Octopus has no unblock message.
- **Ordering**: Octopus's directory protocol relies on point-to-point order within a class
  (`requires_ordering pair_per_class`), which XY provides only with **one VC per class**: the
  preset cannot use Garnet's 4 VCs. MESI_Two_Level's vnets are unordered. With one VC, Octopus's
  non-atomic VC reuse still carries a single flow at the link rate (validation §2: 1 VC depth 4
  reaches 1.0 flit/cycle on Octopus against Garnet's 0.5), but under contention packets queue
  behind each other (radix: 22 cycles of NoC wait per miss, §7).
- **Message classes**: Ruby vnet 0 carries L1 and L2 requests (L1 to L2, L2 to Directory), vnet 1
  responses (data, acks, memory data), vnet 2 L2-to-L1 forwards and invalidations plus L1 unblocks.
  Octopus: request, forward (forwards, invalidations, put-acks and memory reads) and response (data,
  acks, memory writes and write-acks).
- **L1 organisation**: Ruby has one L1 controller per core holding both arrays, one Sequencer (16
  outstanding requests for I and D together, `transitions_per_cycle 4`) and one network interface;
  the Sequencer coalesces requests to a line already outstanding. Octopus has separate L1I and L1D
  controllers, each with its own MSHRs and its own network endpoint (4 endpoints per router, 5 on
  routers 4 and 11).
- **Atomics**: Octopus performs LSE atomics and exclusives in the bridge once the L1 holds the line
  in M; Ruby performs them in the Sequencer and L1 controller.
- **Clocking**: the bridge advances Octopus one cycle per gem5 cycle only while some L1 has a request
  outstanding or a port waits (`octopus.cc` `tick`); with every core idle, Octopus traffic that no
  core waits on (write-backs, memory writes) does not advance. Ruby always runs.

Network interfaces:

- Garnet has an injection and an ejection link of one cycle each and an NI credit loop one cycle
  longer than a router's; Octopus's local links cost nothing. Octopus's packet latency reads 4 cycles
  lower than Garnet's stock network latency for the same path (validation §2).
- Garnet keeps messages that find no idle NI VC in unbounded controller buffers (queueing latency,
  reported separately); Octopus has a 32-packet injection queue per class and back-pressures the
  controller.
- Garnet allocates an output VC atomically (free again only when the tail's credit returns);
  Octopus releases it when the tail leaves.
- SimpleNetwork models no flits, VCs or buffer limits: a message takes the router latency plus the
  link latency plus its size over 16 B/cycle per hop.

Measurement:

- Octopus's `NetworkStats.csv` covers the whole run (it is written at exit), so Octopus network
  totals are whole-run only; `se_arm_ruby.py` dumps the statistics once before the ROI so that the
  Ruby whole-run totals (the three sections summed) compare with them.
- L1 request counts differ by definition: Ruby's Sequencer does not sample a request it coalesced
  with an outstanding one, Octopus logs every request, classic counts every demand access. The
  primary-miss **count** is comparable on all four (§7: within 4 percent); the miss rate is not.

## 5. How to run

```
cd <octopus>
python3 test/gem5_splash/run_matrix.py --bench fft,lu_contig,radix --config all --size small \
    --threads 16 --jobs 6 --out /home/mohassan/Octopus_transfer/runs/gem5_splash_pilot
python3 test/gem5_splash/collect.py /home/mohassan/Octopus_transfer/runs/gem5_splash_pilot
```

- `--bench all --size small,default` is the full matrix (14 programs, 112 runs). `--dry-run` lists
  the commands. A run whose `rc.txt` holds 0 is skipped; `--force` reruns it.
- `--selfcheck` adds the suite's `-t`. LU, RADIX and CHOLESKY check after the ROI, so their ROI is
  unchanged; FFT's check adds an inverse FFT inside the ROI, so the pilot runs FFT without it.
- One directory per run, `<bench>-<size>-p<threads>-<config>[-t]`: gem5's outdir (`stats.txt`,
  `simout.txt`, `config.ini`), `run.json` (the command), `time.txt` (`/usr/bin/time -v`),
  `console.txt`, `rc.txt`. An Octopus run reduces its `LatencyReport_C*.csv` to
  `octopus_requests.json` right after it ends and deletes them (2.3 GB for LU small; `--keep-reports`
  keeps them).
- `collect.py` writes `<root>/results.csv`, one row per run; its docstring defines every column.
- `test/gem5_splash/make_preset.py` regenerates the Octopus preset.

One run by hand, each driver from the benchmark's run directory (the inputs are relative paths):

```
cd /home/mohassan/Octopus_transfer/splash-gem5/bin/m5/fft
<gem5-ruby>/build/ARM_MESI2/gem5.opt -re -d <out> <octopus>/gem5/configs/se_arm_ruby.py \
    --network garnet --num-cores 16 --binary ./FFT -- -p16 -m14
<gem5-noc>/build/ARM/gem5.opt -re -d <out> <octopus>/gem5/configs/se_arm.py --num-cores 16 \
    --octopus-config MultiCoreSystem_gem5_mesh_ruby --octopus-param 'cpu[*].log_requests(i)=1' \
    --binary ./FFT -- -p16 -m14
<gem5-noc>/build/ARM/gem5.opt -re -d <out> <octopus>/gem5/configs/se_arm.py --classic --num-cores 16 \
    --octopus-config MultiCoreSystem_gem5_mesh_ruby --classic-l1 32KiB,8 --classic-l2 8MiB,8 \
    --classic-no-prefetch --dram mcsim_match --binary ./FFT -- -p16 -m14
```

Programs that read stdin (BARNES, FMM, WATER-*, CHOLESKY) take `--stdin FILE` on both drivers.
A SPLASH run with `-pP` uses P threads including the main thread (the `CREATE` macro starts P-1
threads and then runs the worker in the main thread), so P = 16 on 16 cores.

## 6. Validation

`mt_test 16 2000 64` (false sharing, mutex and atomic counters, private work, producer-consumer
exchange; self-checking), 16 cores:

| config | result | ROI cycles | instructions | L1 primary misses | miss latency (cycles) | wall (s) | peak RSS (MB) |
|---|---|---|---|---|---|---|---|
| garnet | PASS | 1.82 M | 8.22 M | 95.9 k | 246 | 35 | 1405 |
| simple | PASS | 1.40 M | 8.21 M | 102.5 k | 226 | 30 | 1406 |
| octopus | PASS | 2.78 M | 2.95 M | 110.2 k | 351 | 95 | 438 |
| classic | PASS | 0.92 M | 5.80 M | 77.2 k | 233 | 19 | 558 |

mt_test is a contention test (its barrier and lock spin), so instruction counts differ with the
timing; it validates correctness, not performance. Octopus's contended misses are about 40 percent
slower than Ruby's here.

SPLASH-3: every pilot run (§7) exits with status 0; LU (`TEST PASSED`) and RADIX (`PASSED: All keys
in place.`) pass their self-checks on all four configurations, and the ROI instruction counts of
each program agree within 0.3 percent across configurations.

## 7. Pilot results

Small inputs, 16 threads on 16 cores, six runs in parallel at nice 19 on a loaded host.
Data: `/home/mohassan/Octopus_transfer/runs/gem5_splash_pilot/results.csv`. Cycles are core
cycles. Miss latency: mean over primary L1 misses (Octopus: LatencyReport rows beyond the L1;
Ruby: Sequencer miss histogram; classic: MSHR miss latency). Network: Garnet
`average_packet_network_latency` and Octopus's packet-weighted class mean, whole run; Ruby
messages = messages delivered to the controllers' network buffers.

| program | config | ROI cycles | vs garnet | L1 primary misses | avg L1 latency | miss latency | ROI packets / flits | run packets | net latency (run) | wall (s) | RSS (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| fft `-m14` | garnet | 303.7 k | 1.00 | 45.4 k | 4.95 | 89.9 | 199 k / 458 k | 466 k | 12.3 (+3.0 queueing) | 28 | 1395 |
| | simple | 281.1 k | 0.93 | 45.6 k | 4.42 | 73.8 | 199 k msgs | 467 k msgs | | 25 | 1395 |
| | octopus | 385.0 k | 1.27 | 46.2 k | 15.5 | 136.4 | | 240 k | 9.2 | 93 | 432 |
| | classic | 299.4 k | 0.99 | 44.4 k | | 164.0 | | | | 18 | 550 |
| lu_contig `-n256` | garnet | 4.246 M | 1.00 | 112.4 k | 2.18 | 51.1 | 406 k / 1.04 M | 954 k | 10.7 (+3.5) | 146 | 1384 |
| | simple | 4.163 M | 0.98 | 113.5 k | 2.13 | 37.5 | 411 k msgs | 963 k msgs | | 127 | 1385 |
| | octopus | 5.311 M | 1.25 | 114.3 k | 2.36 | 60.5 | | 572 k | 6.9 | 441 | 431 |
| | classic | 4.108 M | 0.97 | 109.5 k | | 15.8 | | | | 100 | 546 |
| radix `-n262144` | garnet | 4.850 M | 1.00 | 783.1 k | 7.71 | 95.1 | 4.00 M / 9.54 M | 8.18 M | 18.8 (+4.6) | 220 | 1641 |
| | simple | 4.645 M | 0.96 | 783.3 k | 6.11 | 70.6 | 4.00 M msgs | 8.18 M msgs | | 156 | 1641 |
| | octopus | 5.626 M | 1.16 | 780.1 k | 19.0 | 166.6 | | 3.42 M | 14.3 | 369 | 615 |
| | classic | 6.284 M | 1.30 | 776.4 k | | 292.9 | | | | 128 | 624 |

Octopus per miss (ROI, LatencyReport): fft 8.3 hops and 10.4 cycles of NoC wait, lu_contig 8.3 and
1.0, radix 7.2 and 22.2 (hops and wait summed over the request's packets).

What the pilot shows:

- The matched capacity holds: the four configurations miss in the L1 within 4 percent of each
  other on every program (the 8-line Ruby L1 of §9.4 had 5 times as many misses before the fix).
- Octopus's ROI is 16 to 27 percent longer than Garnet's, entirely from miss latency: Octopus's
  mean miss is 1.2x (LU), 1.5x (FFT) and 1.75x (RADIX) Garnet's, while its hits cost the same.
  On LU, where the network is quiet (1.0 cycle of NoC wait per miss), the gap is 9 cycles per miss
  (60.5 against 51.1): the protocol and controller pipelines. On FFT and RADIX the NoC wait per
  miss (10 and 22 cycles) shows queueing in the single VC per class, the main structural
  difference from Garnet's 4 VCs.
- Garnet's own packet latency is above Octopus's on every program (12.3 against 9.2 on FFT) even
  though Octopus's misses are slower: the 4-cycle interface offset of §4 plus Garnet's 3 to 5
  cycles of queueing before flitisation (not in the 12.3).
- Octopus sends fewer packets: 240 k for the whole FFT run against Garnet's 466 k, 3.4 M against
  8.2 M for RADIX. MESI_Two_Level adds an unblock to most transactions and splits every memory
  access into L2-Directory traffic; Octopus's memory reads and fills also cross the network, so
  the difference is mostly the unblocks and the coalescing.
- SimpleNetwork is 2 to 7 percent faster than Garnet and its miss latency 18 to 27 percent lower:
  no flit serialisation at the routers, no VC allocation, unbounded buffers.
- classic, with its crossbar and no network, matches Garnet on FFT and LU and is 30 percent slower
  on RADIX, whose all-to-all permutation writes queue at the single L2 port.

## 8. Files

New (uncommitted) in this tree:

- `configuration/SystemConfigurations/MultiCoreSystem_gem5_mesh_ruby.csv`: the Octopus preset
  (generated, do not edit by hand)
- `test/gem5_splash/make_preset.py`: writes that preset
- `gem5/configs/se_arm_ruby.py`: Ruby MESI_Two_Level SE driver (Garnet or SimpleNetwork 4x4 mesh)
- `gem5/configs/matched_dram.py`: `mcsim_match` and `ddr4_2400` memories
- `test/gem5_splash/run_matrix.py`, `test/gem5_splash/collect.py`: runner and collector
- `test/gem5_splash/gem5-garnet-functional-read.patch`: the gem5 patch of §9.5
- `docs/gem5_SPLASH_Garnet_vs_Octopus.md`: this document

Modified:

- `gem5/configs/se_arm.py`: `--stdin`, `--dram`, `--classic-l1`, `--classic-l2`,
  `--classic-no-prefetch`; the ROI messages print the tick. Defaults unchanged.
- `gem5/configs/gem5_base_cache_hierarchy.py`: reads the banked presets' `llc_controller[*]` keys
  (§9.2), optional L1/L2 size overrides, optional prefetcher removal (default: prefetchers kept).

In the gem5-ruby worktree: `build_opts/ARM_MESI_Two_Level` (new) and the patch of §9.5 applied to
`src/mem/ruby/network/garnet/GarnetNetwork.{hh,cc}` and `flit.cc`.

## 9. Bugs found

### 9.1 Octopus: a banked LLC indexed its sets with the bank-select bits (fixed)

`CacheDataHandler::calculate_set` (`header/CacheDataHandler.h`) took the set from the line-address
bits right above the offset: `(addr >> log2(block)) & (sets - 1)`. With line-interleaved banks
(`home_interleave_shift 0`), bank k only receives lines whose low `log2(banks)` line-address bits
equal k, so it only ever touched the sets whose low index bits equal k: 1/16 of its sets with 16
banks. `MultiCoreSystem_gem5_mesh` (16 x 512 KiB) therefore had an effective LLC of 512 KiB, not
8 MiB, and every banked network preset lost capacity the same way (2 banks: half).

Fix: the data handler of an LLC bank removes the bank-select digit from the line number before it
takes the set and the tag, and `calculate_address` (the write-back address of an evicted line)
puts it back. The semantics are those of the `llc-banking` branch's address map
(`Interleave::strip/insert`): with N banks interleaved every 2^s lines, line l becomes
`((l >> s) / N << s) | (l & (2^s - 1))`, for any N. `MultiCoreSystem_Mesh` hands each bank the
cores' home map (`home_ids`, `home_interleave_shift`), after checking that every core names
exactly the LLC bank ids with one shift. A bank asked about a line of another bank stops with an
error (the stripped line would alias one of its own). A cache that is not a bank keeps the plain
split, so non-banked presets produce byte-identical outputs.

With the fix, a 512 KiB 8-way bank of `MultiCoreSystem_gem5_mesh` and
`MultiCoreSystem_gem5_mesh_ruby` uses all 1024 sets, set = address bits 10-19, the same mapping as
Ruby's L2 (`start_index_bit` 10). Each of the two banks of `MultiCoreSystem_Grid2B` (16 KiB 2-way,
128 sets) fills all 128 sets on cacheb01 and a2time01, where it filled the 64 sets of one parity
before.

The pilot of §7 ran with a workaround in `MultiCoreSystem_gem5_mesh_ruby.csv`: each bank was
declared 16 times larger (8 MiB, 8-way, 16384 sets), so the 1024 sets a bank used held 8 ways each.
That gives the same set and tag of every line as the fix, so the pilot's Octopus results stand
(§7). The preset now declares the true 512 KiB 8-way per bank.

### 9.2 gem5 config: the classic baseline ignored the banked LLC (fixed)

`gem5_base_cache_hierarchy.py` read `llc_controller.m_data_handler.*`, which the banked mesh presets
do not have (they use `llc_controller[*]`), and silently fell back to a 32 KiB 2-way L2. With
`--octopus-config MultiCoreSystem_gem5_mesh` the classic reference had a 32 KiB L2. It now reads
`llc_controller[*]` and multiplies size and MSHRs by `llc_banks`. The runner also passes
`--classic-l2 8MiB,8` explicitly (the preset's 16 x 512 KiB gives the same size; the option dates
from the §9.1 workaround, when the preset declared 16 x 8 MiB).

### 9.3 gem5 config: the classic baseline had prefetchers (option added)

The stdlib `L1ICache`, `L1DCache` and `L2Cache` default to a `StridePrefetcher`; Octopus has no
prefetcher. `--classic-no-prefetch` removes them; the default is unchanged, so earlier classic
results (including the FS runs) had prefetchers at all three levels.

### 9.4 gem5 stdlib: the MESI_Two_Level L1 holds 8 lines (worked around)

`gem5/components/cachehierarchies/ruby/caches/mesi_two_level/l1_cache.py` passes
`start_index_bit=self._cache_line_size` (64, a byte count) to both L1 `RubyCache`s instead of
`log2(64) = 6`. Every line maps to set 0, so a "32 KiB 8-way" L1 holds 8 lines. The first pilot runs
had 5 times the misses of Octopus and classic (FFT: 225 k against 46 k). `se_arm_ruby.py` sets
`start_index_bit = 6` on both arrays; the L2 (`getIndexBit`) is right. Any stdlib
`MESITwoLevelCacheHierarchy` result in gem5 v25.1 has this L1.

### 9.5 gem5 Garnet: no whole-line functional read (patched)

`GarnetNetwork` implements only the masked `functionalRead(pkt, mask)`; protocols without partial
functional reads (MESI_Two_Level) call the unmasked one, which is `Network`'s `fatal`. An SE
multi-threaded program hits it on its first `futex` on a line in transit. The patch adds
`GarnetNetwork::functionalRead(pkt)`, which walks the same routers, interfaces and links and asks
each message with its unmasked `functionalRead(pkt)`. SimpleNetwork has both and needs nothing.

## 10. How to read the results

The four systems differ in protocol details, endpoints, interfaces and (for classic) the whole
interconnect, so identical cycle counts are not the goal. Compare:

- **Trends across programs**: whether the configurations rank the programs the same way and react
  to communication the same way (LU quiet, FFT transpose, RADIX all-to-all). In the pilot, Octopus
  is the slowest on all three and its gap grows with network load.
- **Per-request latency**: the hit latency is matched (2 cycles on Octopus and Ruby); the miss
  latency is where the systems differ. Split it with Octopus's LatencyReport stage columns and NoC
  Hops / Wait against Ruby's `m_missMachLatencyHistSeqr` (by responding machine) and Garnet's
  queueing and network latency.
- **Miss counts** as the capacity check: if the primary misses diverge by more than a few percent,
  a size, index or inclusion setting differs, before any timing is compared (it found §9.4).
- **Network totals** whole-run only for Octopus, and per message for Ruby; packet counts reflect
  protocol message counts (unblocks, puts, memory traffic), not efficiency alone.
- Garnet's packet latency is about 4 cycles above Octopus's for the same path by construction (§4).

## 11. Full-matrix estimate

From the pilot wall times (each at about one sixth of the host, with six runs in parallel): per
program the four configurations of a small input cost 163 s (FFT), 813 s (LU) and 873 s (RADIX) of
serial time, in the ratio Octopus 2.3 : Garnet 1.0 : Simple 0.8 : classic 0.6. The pilot kernels are
the short programs; the applications (BARNES, FMM, OCEAN, RADIOSITY, RAYTRACE, VOLREND, WATER) are
assumed twice the pilot mean, 1230 s. Default inputs are scaled by the program's native
single-thread time ratio default / small (BARNES 10.2, VOLREND 27, LU 5.7, OCEAN 1.9 to 6.7, the rest
1.6 to 6.5). That gives:

| part | runs | serial time | wall at 6 jobs |
|---|---|---|---|
| small inputs | 56 | about 4.3 h | about 0.8 h |
| default inputs | 56 | about 28 h | about 4.7 h |
| full matrix | 112 | about 32 h | about 5.5 to 6 h |

The longest single run, VOLREND default on Octopus, is estimated at about 4.5 h and bounds the wall
time from below. The estimate carries a factor of 2 to 3 of uncertainty (instruction counts of the
applications at 16 threads are unknown, and barrier spinning grows with the thread count), so plan
for 4 to 15 hours. Disk: an Octopus default-input run writes its LatencyReports (2.3 GB for LU small,
probably tens of GB for the default inputs) before the runner reduces and deletes them; six in
parallel need that headroom (788 GB free at the time of the pilot). Peak memory per run stays under
2 GB (Ruby about 1.4 to 1.7 GB, Octopus and classic about 0.4 to 0.6 GB on the small inputs).
