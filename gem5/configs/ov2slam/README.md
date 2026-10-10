# ov2slam checkpoints in two stages

Booting the image and starting the ROS 2 launch costs about three hours on the
atomic CPU; the bag clip and its flags cost minutes. These files split the two so
that a new clip, new bag flags or a new parameter file never repeat the boot.

| file | role |
|---|---|
| `ov2slam_generic.rcS` | guest script: environment, node-only launch, `m5 checkpoint`, then a second `m5 readfile` for the run parameters, `ros2 bag play`, `m5 writefile ov2slam_traj.txt` |
| `params_<clip>.sh` | what the second readfile delivers: `CLIP=` and `BAG_ARGS=`; must start with `# OV2_PARAMS` |
| `checkpoints.py` | gem5 config for both stages, same board as `fs_arm.py` with atomic cores and no caches |

```
# once per image or node binary: boot, start the node, checkpoint (~3 h)
gem5.opt checkpoints.py --stage env --resources /opt/gem5-resources
# per clip: restore, play, checkpoint at the first stereo pair, run to the end (~20 min + clip)
gem5.opt checkpoints.py --stage roi --clip 1s --resources /opt/gem5-resources
# measured run: O3 + Octopus from that checkpoint (add --classic for the gem5 baseline)
gem5.opt ../fs_arm.py --clip 1s --resources /opt/gem5-resources
```

Checkpoints land next to the resources (`--checkpoint-dir` to change that):
`ov2_env_trim` after stage env, `ov2_start_trim_<clip>` after stage roi. The
roi stage continues to the end of the clip as a functional check unless
`--stop-at-roi` is given; on the atomic model the node may skip a frame, which
does not happen on O3 from the same checkpoint.

A checkpoint is tied to the disk image it was taken on. Changing files in the
image, including the node binary, needs a new `env` stage.
