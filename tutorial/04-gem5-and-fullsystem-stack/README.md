# 04 — gem5 and the full-system stack

Everything so far ran Octopus **standalone**: a trace-driven `CPU` object feeds the
memory hierarchy. Here the request source is a real CPU model — gem5 — and the same
hierarchy, unchanged, sits behind it.

| folder | what | time | status |
|---|---|---|---|
| `01-gem5-se/` | gem5 in syscall-emulation mode driving Octopus: a self-checking test, then a real-time SLAM under memory interference and two mitigations (round-robin arbitration, LLC way partitioning) | 20 min + runs in the background | ready |
| `02-full-system-arm/` | ARM Linux under gem5, restored from a checkpoint, with Octopus as its memory system | 25–30 min + take-home | plan: the checkpoint and `prewarm.sh` are not in place yet |

## What the dev container has

| | |
|---|---|
| `gem5` | on the `PATH`: gem5 25.1.0.1 for ARM with Octopus linked in (`gem5 -re -d <out> <config.py> ...`) |
| `/opt/gem5-resources/` | the ARM kernel, bootloader and Ubuntu disk image for full system (`gem5/configs/fs_arm.py`) |
| `g++-aarch64-linux-gnu` | the cross compiler for the workloads; their m5ops are in `gem5/m5ops/`, so no gem5 checkout is needed to rebuild them |
| prebuilt workloads | `gem5/se_test/se_test-static` and `01-gem5-se/slam_demo/slam_demo`, the binaries the reference results were made with |

`bash 01-gem5-se/check.sh` (about 5 minutes) tells you whether gem5 and Octopus work
together in your environment.

## Octopus inside gem5

gem5 loads Octopus as a shared library, `build/libOctopus.so`, the one the container
builds from your checkout. A change to Octopus's own code — a new arbiter from
`03-extending-octopus/01-arbiter`, a protocol table, a preset — reaches gem5 with the
usual `cmake --build build -j$(nproc)`: about 50 seconds, and the next `gem5` run uses
it.

What does need gem5 rebuilt is a change to the bridge itself (`gem5/octopus.cc`,
`gem5/octopus.hh`, `gem5/Octopus.py`) or to the Octopus headers it compiles in
(`header/ExternalCPU.h`, `header/CacheSim.h` and what they include). The container
carries the gem5 binary, not its source, so that work happens outside it.

## gem5 outside the container

`gem5/get_gem5.sh` builds the same gem5 from scratch: it clones gem5 v25.1.0.1,
applies the patch in `gem5/patches/`, and builds it with this repository as an
`EXTRAS` module. Build Octopus first; the gem5 build itself is long, tens of
minutes even on many cores:

```shell
bash gem5/get_gem5.sh                # clones into ../gem5, beside this repository
export GEM5_ROOT=<that directory>    # the tutorial scripts then use $GEM5_ROOT/build/ARM/gem5.opt
```

Use `$GEM5_ROOT/build/ARM/gem5.opt` wherever the exercises say `gem5`. The full-system
exercise also needs the kernel, bootloader and disk image under `/opt/gem5-resources/`
(see `gem5/configs/fs_arm.py`).
