"""Two-stage checkpoints for the ov2slam full-system workload.

Stage ``env`` boots the image once on atomic cores with no caches, starts the
ov2slam node through ``ov2slam_generic.rcS`` and saves ``ov2_env_trim`` when
the script reaches ``m5 checkpoint``. Stage ``roi`` restores that checkpoint
with ``params_<clip>.sh`` as the readfile: the script's second ``m5 readfile``
picks it up, plays the clip, and the first stereo pair (``m5_work_begin`` in
the node) saves ``ov2_start_trim_<clip>``. ``fs_arm.py`` then restores
``ov2_start_trim_<clip>`` for the measured O3 + Octopus run.

The board must match fs_arm.py (same memory, platform, release, kernel
arguments) so its checkpoints restore there.

    gem5.opt checkpoints.py --stage env --resources /opt/gem5-resources
    gem5.opt checkpoints.py --stage roi --clip 1s --resources /opt/gem5-resources
"""
import argparse
import os

import m5
from gem5.components.boards.arm_board import ArmBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    BootloaderResource,
    CheckpointResource,
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.simulator import Simulator
from m5.objects import ArmDefaultRelease, VExpress_GEM5_Foundation

HERE = os.path.dirname(os.path.abspath(__file__))

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--stage", choices=["env", "roi"], required=True)
parser.add_argument("--clip", choices=["0.5s", "1s", "3s"], default="1s",
                    help="which params_<clip>.sh the roi stage delivers")
parser.add_argument("--resources", default="/opt/gem5-resources",
                    help="directory with the kernel, bootloader and disk image")
parser.add_argument("--image", default="arm-ubuntu-22.04-trim.img")
parser.add_argument("--checkpoint-dir", default=None,
                    help="where checkpoints are read and written (default: --resources)")
parser.add_argument("--stop-at-roi", action="store_true",
                    help="roi stage: stop once ov2_start_trim_<clip> is written instead of running the clip to its end")
args = parser.parse_args()
ckpt_dir = args.checkpoint_dir or args.resources
env_ckpt = os.path.join(ckpt_dir, "ov2_env_trim")
roi_ckpt = os.path.join(ckpt_dir, f"ov2_start_trim_{args.clip}")

# Same board as fs_arm.py, with the functional CPU and no caches.
processor = SimpleProcessor(cpu_type=CPUTypes.ATOMIC, num_cores=4, isa=ISA.ARM)
board = ArmBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=DualChannelDDR4_2400(size="8GiB"),
    cache_hierarchy=NoCache(),
    release=ArmDefaultRelease(),
    platform=VExpress_GEM5_Foundation(),
)
kernel_cmd = [
    "earlyprintk", "earlycon=pl011,0x1c090000", "console=ttyAMA0",
    "lpj=19988480", "norandmaps", "loglevel=8", "mem=6GB", "root=/dev/vda2",
    "rw", "init=/sbin/gem5_init.sh", "vmalloc=768MB", "no_systemd",
]
workload = dict(
    kernel=KernelResource(os.path.join(args.resources, "arm64-linux-kernel-5.15.180")),
    disk_image=DiskImageResource(os.path.join(args.resources, args.image)),
    bootloader=BootloaderResource(os.path.join(args.resources, "boot_foundation.arm64-20220707")),
    kernel_args=kernel_cmd,
)
if args.stage == "env":
    workload["readfile"] = os.path.join(HERE, "ov2slam_generic.rcS")
else:
    workload["readfile"] = os.path.join(HERE, f"params_{args.clip}.sh")
    workload["checkpoint"] = CheckpointResource(env_ckpt)
board.set_kernel_disk_workload(**workload)


def handle_checkpoint():          # guest `m5 checkpoint`: environment ready
    print(f"Environment ready: saving {env_ckpt}")
    simulator.save_checkpoint(env_ckpt)
    yield True


def handle_workbegin():           # first stereo pair reached the node
    print(f"ROI start: saving {roi_ckpt}")
    m5.stats.reset()
    simulator.save_checkpoint(roi_ckpt)
    yield args.stop_at_roi


def handle_workend():
    print("ROI end: dumping stats")
    m5.stats.dump()
    yield False


def exit_event_handler():         # same three exits as fs_arm.py
    print("first exit event: Kernel booted")
    yield False
    print("second exit event: In after boot")
    yield False
    print("third exit event: After run script")
    yield True


simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.CHECKPOINT: handle_checkpoint(),
        ExitEvent.WORKBEGIN: handle_workbegin(),
        ExitEvent.WORKEND: handle_workend(),
        ExitEvent.EXIT: exit_event_handler(),
    },
)
simulator.run()
