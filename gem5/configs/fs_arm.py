"""ARM full-system run of the ov2slam workload on O3 cores with the Octopus
cache hierarchy (or the gem5 classic baseline with --classic).

The run restores an ROI checkpoint made by ov2slam/checkpoints.py and measures
from there: stats are reset at the workload's workbegin marker and dumped at
its workend marker. Resources (kernel, bootloader, disk image, ATP files,
checkpoints) live in one directory, /opt/gem5-resources in the container.

    gem5.opt fs_arm.py --clip 1s                      # restore ov2_start_trim_1s, Octopus
    gem5.opt fs_arm.py --clip 0.5s --classic          # same clip, classic caches
    gem5.opt fs_arm.py --resources /path --clip 3s    # resources elsewhere
    gem5.opt fs_arm.py --no-checkpoint --readfile x.rcS   # fresh boot with your own script
"""
import argparse
import os

import m5
from gem5.components.boards.arm_board import ArmBoard
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
from gem5.simulate.simulator import ExitEvent, Simulator
from gem5.utils.requires import requires
from m5.objects import ArmDefaultRelease, VExpress_GEM5_Foundation

from gem5_base_cache_hierarchy import Gem5BaseCacheHierarchy
from octopus_cache_hierarchy import OctopusCacheHierarchy

requires(isa_required=ISA.ARM)

HERE = os.path.dirname(os.path.abspath(__file__))

parser = argparse.ArgumentParser(
    description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
)
parser.add_argument(
    "--octopus-config",
    type=str,
    default="MultiCoreSystem_gem5",
    help="Octopus system preset: configuration/SystemConfigurations/<name>.csv",
)
parser.add_argument(
    "--octopus-out",
    type=str,
    default=None,
    help="Directory for Octopus's own logs (default: gem5's outdir)",
)
parser.add_argument(
    "--classic",
    action="store_true",
    help="Use the gem5 classic cache baseline sized from the same preset instead of Octopus",
)
parser.add_argument(
    "--octopus-param",
    action="append",
    default=[],
    metavar="NAME(TYPE)=VALUE",
    help="Octopus override applied on top of the preset; repeatable. "
         "E.g. cache_controller[*].protocol_type(s)=SNOOP_MESI",
)
parser.add_argument(
    "--cache-ports",
    type=str,
    default=None,
    metavar="LOADS,STORES",
    help="O3 cacheLoadPorts,cacheStorePorts per cycle (gem5 default 200,200; "
         "2,1 is a typical L1)",
)
parser.add_argument(
    "--max-ticks",
    type=int,
    default=None,
    help="Stop after this many ticks (relative to the start), e.g. for a short debug trace",
)
parser.add_argument(
    "--resources",
    default="/opt/gem5-resources",
    help="directory with the kernel, bootloader, disk image, atp_files/ and the checkpoints",
)
parser.add_argument(
    "--image", default="arm-ubuntu-22.04-trim.img", help="disk image file name under --resources"
)
parser.add_argument(
    "--clip",
    choices=["0.5s", "1s", "3s"],
    default="1s",
    help="which ROI checkpoint to restore (ov2_start_trim_<clip>, made by ov2slam/checkpoints.py)",
)
parser.add_argument(
    "--checkpoint", default=None, help="restore this checkpoint directory instead of the clip's"
)
parser.add_argument(
    "--no-checkpoint", action="store_true", help="boot from scratch instead of restoring"
)
parser.add_argument(
    "--readfile",
    default=None,
    help="guest script delivered by `m5 readfile` (default: ov2slam/params_<clip>.sh, the "
         "file the restored script already consumed)",
)
parser.add_argument(
    "--exit-at-workend",
    action="store_true",
    help="stop at the workend marker instead of running on to the end of the clip, the "
         "trajectory write-out and the guest's own exit",
)
args = parser.parse_args()
octopus_out = args.octopus_out or m5.options.outdir


def get_atp_files(atp_files_path):
    atp_files = set()
    for path in atp_files_path:
        for dname, _, fnames in os.walk(path):
            atp_files.update([os.path.join(dname, fname) for fname in fnames])
    return list(atp_files)


atp_files = get_atp_files([os.path.join(args.resources, "atp_files")])

if args.classic:
    cache_hierarchy = Gem5BaseCacheHierarchy(args.octopus_config, octopus_out)
else:
    cache_hierarchy = OctopusCacheHierarchy(
        args.octopus_config, octopus_out, extra_params=args.octopus_param
    )

memory = DualChannelDDR4_2400(size="8GiB")

processor = SimpleProcessor(cpu_type=CPUTypes.O3, num_cores=4, isa=ISA.ARM)
if args.cache_ports:
    _loads, _stores = (int(x) for x in args.cache_ports.split(","))
    for _c in processor.get_cores():
        _c.core.cacheLoadPorts = _loads
        _c.core.cacheStorePorts = _stores

# Armv8 default release; the Foundation platform matches the kernel and
# bootloader shipped with the resources.
board = ArmBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
    release=ArmDefaultRelease(),
    platform=VExpress_GEM5_Foundation(),
)

kernel_cmd = [
    "earlyprintk",
    "earlycon=pl011,0x1c090000",
    "console=ttyAMA0",
    "lpj=19988480",
    "norandmaps",
    "loglevel=8",
    "mem=6GB",
    "root=/dev/vda2",
    "rw",
    "init=/sbin/gem5_init.sh",
    "vmalloc=768MB",
    "no_systemd",
]

readfile = args.readfile or os.path.join(HERE, "ov2slam", f"params_{args.clip}.sh")
workload = dict(
    kernel=KernelResource(os.path.join(args.resources, "arm64-linux-kernel-5.15.180")),
    disk_image=DiskImageResource(os.path.join(args.resources, args.image)),
    bootloader=BootloaderResource(os.path.join(args.resources, "boot_foundation.arm64-20220707")),
    readfile=readfile,
    kernel_args=kernel_cmd,
)
if not args.no_checkpoint:
    checkpoint = args.checkpoint or os.path.join(args.resources, f"ov2_start_trim_{args.clip}")
    workload["checkpoint"] = CheckpointResource(checkpoint)
board.set_kernel_disk_workload(**workload)


def handle_workbegin():
    # Fires only on a fresh boot; a restored ROI checkpoint already sits
    # past this marker (ov2slam/checkpoints.py took it here).
    print("Resetting stats at the start of ROI!")
    m5.stats.reset()
    yield False


def handle_workend():
    print("Dump stats at the end of the ROI!")
    m5.stats.dump()
    yield args.exit_at_workend


def exit_event_handler():
    print("first exit event: Kernel booted")
    yield False
    print("second exit event: In after boot")
    yield False
    print("third exit event: After run script")
    yield True


simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.WORKBEGIN: handle_workbegin(),
        ExitEvent.WORKEND: handle_workend(),
        ExitEvent.EXIT: exit_event_handler(),
    },
)

print(atp_files)
if args.max_ticks:
    simulator.run(max_ticks=args.max_ticks)
else:
    simulator.run()
