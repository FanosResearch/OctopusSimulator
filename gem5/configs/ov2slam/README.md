# ov2slam checkpoints in two stages

Booting the image and starting the ROS 2 launch costs about three hours on the
atomic CPU; the bag clip and its flags cost minutes. These files split the two so
that a new clip, new bag flags or a new parameter file never repeat the boot.

| file | role |
|---|---|
| `ov2slam_generic.rcS` | guest script: environment, node-only launch, `m5 checkpoint`, then a second `m5 readfile` for the run parameters, `ros2 bag play`, `m5 writefile ov2slam_traj.txt` |
| `params_<clip>.sh` | what the second readfile delivers: `CLIP=` and `BAG_ARGS=`; must start with `# OV2_PARAMS` |
| `params_3s_rate2.sh` | the 3 s clip played at twice its rate (`--rate 2`, frames arrive every 25 ms); BAG_ARGS are fixed in the roi checkpoint, so it needs its own: `checkpoints.py --stage roi --clip 3s --params params_3s_rate2.sh --suffix _rate2`, then `fs_arm.py --checkpoint <resources>/ov2_start_trim_3s_rate2` |
| `params_v102_4s5_rate2.sh` | EuRoC V1_02 from +37.5 s for 4.5 s at `--rate 2` (fast motion, about 71 deg/s): the clip where the ATP aggressors cost accuracy (ATE RMSE 0.044 m solo, 0.88 m with ATP). The clip is not in the stock image: it lives in a copy, `arm-ubuntu-22.04-trim-v102.img`, with Vicon ground truth as a PointStamped on `/vicon/position`. Build both checkpoints against that image: `checkpoints.py --stage env` and `--stage roi --clip 3s --params params_v102_4s5_rate2.sh --suffix _v102_4s5_rate2 --stop-at-roi`, each with `--image arm-ubuntu-22.04-trim-v102.img --checkpoint-dir <resources>/v102` |
| `params_v102_3s_rate2.sh` | V1_02 from +41 s for 3 s at `--rate 2`, same image; ov2slam crashes on it (the clip starts mid-turn in a low-texture room), kept as the negative case |
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
