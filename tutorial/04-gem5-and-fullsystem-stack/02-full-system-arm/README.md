# 04/02 — Full-system ARM Linux on Octopus

**Goal:** run ARM Linux on gem5 with Octopus as its memory system, and see the two
ways to skip what you do not want to measure. The big workload, ov2slam (a stereo
visual SLAM under ROS 2, fed by a bag clip), starts from a **checkpoint** taken
inside the running system. A small one, `se_test` from 01, boots from scratch on
fast cores and **switches** to the detailed ones where its measured region begins.

## Before you start

The dev container carries the kernel, the bootloader, the ROI checkpoints and the
disk image, compressed. Unpack the image once (about a minute; run it again after
the codespace was stopped, it returns at once when the image is still there):

```shell
bash gem5/get_disk_image.sh
```

## From SE to full system: what changes in the config

Compare `gem5/configs/se_arm.py` (01) with `gem5/configs/fs_arm.py`. The memory
system is the same: the `MultiCoreSystem_gem5` preset through
`OctopusCacheHierarchy`, with `--octopus-param` and `--classic` as in 01. What
changes is everything around it:

| | SE (`se_arm.py`) | full system (`fs_arm.py`) |
|---|---|---|
| board | `SimpleBoard`: no platform, no devices | `ArmBoard` with the `VExpress_GEM5_Foundation` platform |
| workload | one static binary, syscalls emulated by gem5 | kernel + bootloader + disk image; Linux runs the program |
| what runs in the guest | the binary | the script `m5 readfile` delivers (`--readfile`) |
| threads and cores | one thread per core, no scheduler | Linux schedules; pin with `taskset` |
| start of the measurement | the program starts at once | a checkpoint, or a switch of CPU model |

## Part A — ov2slam from a checkpoint

`fs_arm.py` restores the checkpoint of a clip and runs it on O3 cores with the
Octopus hierarchy. It runs for a long time (over an hour for the 0.5 s clip), so
start it in the background:

```shell
gem5 -re -d m5out_fs gem5/configs/fs_arm.py --clip 0.5s &   # clips: 0.5s, 1s, 3s
tail -f m5out_fs/board.terminal                              # the guest's console
```

- **The checkpoint** (`/opt/gem5-resources/ov2_start_trim_<clip>`) was taken
  past the Linux boot and the ROS 2 start, at the clip's first stereo pair:
  booting is separated from measuring.
- **The measured region** is the workload's own: gem5's statistics are reset at
  its work-begin marker and dumped at its work-end marker, and the node's
  trajectory is copied out of the guest at the end (`m5 writefile`, into
  `m5out_fs/ov2slam_traj.txt`).
- **In `board.terminal`**, every processed stereo pair prints a `Full-Front_End`
  line and every keyframe a `New Keyframe send to Back-End` line. The node
  crashes at exit (`corrupted size vs. prev_size`) after it has written its
  results; that is a known shutdown bug of the node, not of the simulator.

## Part B — how the checkpoints are made

Booting the image and starting the ROS 2 node cost about three hours on gem5's
atomic CPU; a clip costs minutes. `gem5/configs/ov2slam/` therefore takes the
checkpoints in two stages:

1. **env**, once per disk image: boot, start the node, `m5 checkpoint`
   (about 3 h).
2. **roi**, once per clip: restore env, receive the clip and the bag flags from a
   *second* `m5 readfile`, play the bag, checkpoint at the first stereo pair
   (about 20 min).

```shell
gem5 gem5/configs/ov2slam/checkpoints.py --stage env
gem5 gem5/configs/ov2slam/checkpoints.py --stage roi --clip 1s --stop-at-roi
```

Both stages run on atomic cores without caches: they only have to get the guest
to the right point, not time it. The container ships the three roi checkpoints
but not env, so making your own starts with the 3-hour env stage (add
`--checkpoint-dir <dir>` to keep them out of `/opt/gem5-resources`).
`gem5/configs/ov2slam/README.md` has the files and the details. A checkpoint is
tied to the disk image it was taken on.

## Part C — no checkpoint: boot on atomic, switch to O3

The other way to skip the boot is to run it on a fast CPU model and switch to the
detailed one where the measurement begins. `--switch-at-workbegin` boots the image
from scratch on atomic cores, runs `/se_test` (the 01 test, also inside the image)
and switches all cores to O3 + Octopus at its work-begin marker:

```shell
gem5 -re -d m5out_switch gem5/configs/fs_arm.py --switch-at-workbegin
tail -f m5out_switch/board.terminal          # boot, then se_test: RESULT: PASS
```

Almost all of its time is the atomic boot: about 6 minutes on a desktop machine,
longer in a codespace.

- **Atomic cores bypass Octopus.** In atomic mode the bridge answers every access
  at once and the engine does not run, so the boot costs what it would without
  Octopus.
- **The switch happens at work-begin.** The handler calls `processor.switch()`,
  then resets the statistics, so `stats.txt` covers only se_test's measured
  region, on O3 + Octopus.
- **Only atomic → O3.** Switching the other way would hand a core over while
  Octopus still has its requests in flight; the bridge does not wait for them
  yet.
- **Not with the shipped checkpoints.** The switchable processor names its cores
  differently from the one the checkpoints were taken with, and gem5 skips what
  it cannot match without a word. Switching is for a fresh boot.

Run your own program the same way with `--readfile my.rcS`: the script runs in
the guest after boot, and the first `m5 workbegin` in the program switches the
cores.
