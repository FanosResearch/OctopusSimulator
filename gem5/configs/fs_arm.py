"""ARM full-system run of the ov2slam workload on O3 cores with the Octopus
cache hierarchy (or the gem5 classic baseline with --classic).

The run restores an ROI checkpoint made by ov2slam/checkpoints.py and measures
from there: stats are reset at the workload's workbegin marker and dumped at
its workend marker. Resources (kernel, bootloader, disk image, ATP files,
checkpoints) live in one directory, /opt/gem5-resources in the container.

--atp adds Arm ATP traffic generators as aggressors: ATP's GPU (4K frame
buffer, 19.9 GB/s) and DPU (four display layers, 1.24 GB/s each) example
profiles, gem5/configs/atp/. Each ATP master drives its own Octopus bridge, so
its requests contend with the cores in Octopus's bus, LLC and DRAM model. The
streams loop for as long as the workload runs and never end the simulation.
They address the top 2 GiB of DRAM, which Linux does not use (mem=6GB). It
needs a gem5 built with ATP-Engine as an EXTRAS module.

    gem5.opt fs_arm.py --clip 1s                      # restore ov2_start_trim_1s, Octopus
    gem5.opt fs_arm.py --clip 0.5s --classic          # same clip, classic caches
    gem5.opt fs_arm.py --resources /path --clip 3s    # resources elsewhere
    gem5.opt fs_arm.py --no-checkpoint --readfile x.rcS   # fresh boot with your own script
    gem5.opt fs_arm.py --switch-at-workbegin          # fresh boot on atomic cores, /se_test on O3
    gem5.opt fs_arm.py --clip 1s --atp                # plus GPU and DPU aggressors (ATP)

--switch-at-workbegin is the other way to skip the boot: no checkpoint, the
image boots on atomic cores (Octopus bypassed) and the cores switch to O3 at
the workload's workbegin marker. It runs se_test_switch.rcS unless --readfile
says otherwise.
"""
import argparse
import os

import m5
from gem5.components.boards.arm_board import ArmBoard
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.simple_switchable_processor import (
    SimpleSwitchableProcessor,
)
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
    default=None,
    help="Octopus system preset: configuration/SystemConfigurations/<name>.csv "
         "(default MultiCoreSystem_gem5, or MultiCoreSystem_gem5_atp with --atp)",
)
parser.add_argument(
    "--octopus-out",
    type=str,
    default=None,
    help="Directory for Octopus's own logs (default: gem5's outdir)",
)
parser.add_argument(
    "--atp",
    action="store_true",
    help="Add the ATP GPU and DPU aggressors (gem5/configs/atp/) on their own Octopus "
         "bridges; implies --octopus-config MultiCoreSystem_gem5_atp unless one is given",
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
    help="directory with the kernel, bootloader, disk image and the checkpoints",
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
parser.add_argument(
    "--switch-at-workbegin",
    action="store_true",
    help="boot from scratch on atomic cores and switch to O3 at the workload's workbegin "
         "marker; runs se_test_switch.rcS unless --readfile is given",
)
args = parser.parse_args()
if args.switch_at_workbegin:
    # The switchable processor names its cores start/switch, a checkpoint from
    # SimpleProcessor names them cores, and a restore silently skips sections
    # it cannot match: switching is for a fresh boot only.
    if args.checkpoint:
        parser.error("--switch-at-workbegin boots from scratch; drop --checkpoint")
    if args.cache_ports:
        parser.error("--cache-ports is not supported with --switch-at-workbegin")
    args.no_checkpoint = True
if args.atp and args.classic:
    parser.error("--atp needs the Octopus hierarchy; the --classic baseline has no ATP bridges")
if args.octopus_config is None:
    args.octopus_config = "MultiCoreSystem_gem5_atp" if args.atp else "MultiCoreSystem_gem5"
octopus_out = args.octopus_out or m5.options.outdir

# ATP aggressors. DRAM spans 0x80000000-0x280000000 (8 GiB); Linux uses the first
# 6 GiB (mem=6GB below), so the streams live in 0x200000000-0x280000000. Each
# stream gets its own window; ATP fits the profile's addresses into it.
MAX_ADDR = 2**64 - 1
ATP_BASE = 0x200000000
ATP = dict(
    files=[os.path.join(HERE, "atp", f) for f in ("gpu.atp", "dpu.atp")],
    masters=["GPU", "DPU_1"],
    streams=[
        # GPU: one stream rooted at GPU_READS; GPU_WRITES is gated on its send
        # status (wait_for PROFILE_LOCKED) and takes the write window
        ("GPU", "GPU_READS", ATP_BASE, 0x4000000, ATP_BASE + 0x4000000, 0x4000000),
    ] + [
        # DPU: four independent display layers, reads only
        ("DPU_1", f"DPU_1_LAYER_{k}", ATP_BASE + 0x10000000 + (k - 1) * 0x8000000,
         0x8000000, MAX_ADDR, MAX_ADDR)
        for k in range(1, 5)
    ],
)

if args.classic:
    cache_hierarchy = Gem5BaseCacheHierarchy(args.octopus_config, octopus_out)
else:
    cache_hierarchy = OctopusCacheHierarchy(
        args.octopus_config, octopus_out, atp=ATP if args.atp else None,
        extra_params=args.octopus_param,
    )

memory = DualChannelDDR4_2400(size="8GiB")

if args.switch_at_workbegin:
    # Atomic first: the bridge bypasses the Octopus engine in atomic mode, so
    # the boot runs fast. Atomic -> O3 needs no drain of the bridge.
    processor = SimpleSwitchableProcessor(
        starting_core_type=CPUTypes.ATOMIC,
        switch_core_type=CPUTypes.O3,
        num_cores=4,
        isa=ISA.ARM,
    )
else:
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

if args.switch_at_workbegin:
    readfile = args.readfile or os.path.join(HERE, "se_test_switch.rcS")
else:
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
    # With --switch-at-workbegin, the first marker switches to O3; switch()
    # toggles, so later markers must not call it again.
    if args.switch_at_workbegin:
        print("Switching ATOMIC -> O3 at the start of ROI!")
        processor.switch()
    print("Resetting stats at the start of ROI!")
    m5.stats.reset()
    yield False
    while True:
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


def exit_after_script():
    # The image's boot exits are gem5-bridge hypercalls with their own
    # handlers; the first classic exit is the guest script's `m5 exit`.
    print("exit event: guest script done")
    yield True


simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.WORKBEGIN: handle_workbegin(),
        ExitEvent.WORKEND: handle_workend(),
        ExitEvent.EXIT: exit_after_script() if args.switch_at_workbegin else exit_event_handler(),
    },
)

if args.max_ticks:
    simulator.run(max_ticks=args.max_ticks)
else:
    simulator.run()
