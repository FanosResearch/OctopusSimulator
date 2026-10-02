# 04/02 — Full-system ARM Linux on Octopus

**Goal:** see the full-system capability without booting Linux in the room. A
checkpoint taken inside a running ARM Ubuntu is restored, an ov2slam workload
(a stereo visual SLAM under ROS 2, fed by a bag clip) runs on a real kernel with
Octopus as its memory system, and you inspect it while it runs.

## Before you start

The dev container carries the kernel, the bootloader, the ROI checkpoints and the
disk image, compressed. Unpack the image once (about a minute; run it again after
the codespace was stopped, it returns at once when the image is still there):

```shell
bash gem5/get_disk_image.sh
```

## The run

`gem5/configs/fs_arm.py` restores the checkpoint of a clip and runs it on O3 cores
with the Octopus hierarchy (the `MultiCoreSystem_gem5` preset, as in 01). Start it
in the background; it runs for a long time:

```shell
gem5 -re -d m5out_fs gem5/configs/fs_arm.py --clip 0.5s &   # clips: 0.5s, 1s, 3s
tail -f m5out_fs/simout.txt
```

- **The checkpoint** (`/opt/gem5-resources/ov2_start_trim_<clip>`) was taken
  past the Linux boot and the ROS 2 start, at the clip's first stereo pair:
  booting is separated from measuring.
- **The measured region** is the workload's own: gem5's statistics are reset at
  its work-begin marker and dumped at its work-end marker, and the node's
  trajectory is copied out of the guest at the end (`m5 writefile`).
- **Everything from 01 applies:** `--octopus-param` changes the hierarchy without
  rebuilding, and `--classic` runs the same clip on gem5's own caches as the
  baseline.

## Take-home: your own checkpoints

`gem5/configs/ov2slam/README.md` is the full journey: boot the image and start
the node once (`checkpoints.py --stage env`), then one ROI checkpoint per clip
(`--stage roi --clip <clip>`), with the durations of each stage.
