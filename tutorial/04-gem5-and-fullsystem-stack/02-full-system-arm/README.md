# 04/02 — Full-system ARM Linux on Octopus

> Pending the gem5 environment. Steps below are the plan.

**Goal:** see the full-system capability without booting Linux in the room. A
checkpoint is restored, a workload runs on a real kernel with Octopus as its memory
system, and you inspect it live. 25–30 minutes, plus a take-home.

## How the segment runs

1. **At the start of the second block**, before this exercise begins, everyone
   launches the pre-warm in the background:

   ```shell
   bash tutorial/04-gem5-and-fullsystem-stack/02-full-system-arm/prewarm.sh &
   ```

   It restores a checkpoint taken past Linux boot and starts the workload, logging
   to `log/fs_arm/frames.log`. By the time we reach this exercise, 40–60 minutes of
   simulated progress exists in your own environment.

2. The walkthrough covers what the checkpoint contains, why booting is separated
   from measuring, and how the ARM specifics (LL/SC, LSE atomics) reach the
   coherence protocol.

3. Then everyone tails their own log:

   ```shell
   tail -f log/fs_arm/frames.log
   ```

   and opens the `LatencyReport` it is writing.

The same pre-warm runs on the podium machine; if yours is behind, follow that one.

## Take-home

The full journey — boot the kernel, reach the region of interest, write the
checkpoint — with the commands and the real durations, so you can reproduce the
checkpoint yourself. Written up here once the environment is final.

## Reference

`gem5/cmds.md` on the `gem5_ARM_Challenge` branch is the working record of how the
pieces fit today: the Apptainer image, the bind mounts, the `scons EXTRAS` build, the
`LD_LIBRARY_PATH`, and the `fs_arm.py --octopus-xml` invocation.
