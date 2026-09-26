import m5
from m5.debug import flags
# m5.util.addToPath("../../")
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.processors.cpu_types import CPUTypes

from gem5.isas import ISA
from m5.objects import ArmDefaultRelease
from gem5.utils.requires import requires
from gem5.resources.workload import Workload
from gem5.resources.resource import *
from gem5.simulate.simulator import (
    ExitEvent,
    Simulator,
)
from m5.objects import VExpress_GEM5_Foundation, VExpress_GEM5_V1
from gem5.coherence_protocol import CoherenceProtocol
from gem5.components.boards.arm_board import ArmBoard
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.simple_switchable_processor import SimpleSwitchableProcessor
from gem5.components.boards.abstract_board import AbstractBoard
from gem5.resources.resource import (
    KernelResource,
    BootloaderResource,
    DiskImageResource,
    CheckpointResource,
)
import argparse

from octopus_cache_hierarchy import OctopusCacheHierarchy
from gem5_base_cache_hierarchy import Gem5BaseCacheHierarchy

requires(isa_required=ISA.ARM)

import os
def get_atp_files(atp_files_path):
    atp_files = set()
    for path in atp_files_path:
        for dname, _, fnames in os.walk(path):
            atp_files.update([os.path.join(dname, fname) for fname in fnames])
    return list(atp_files)
# Here we setup the parameters of the l1 and l2 caches.
atp_files = get_atp_files(["/home/guotong/gem5_resource/atp_files"])

# Add argument parser for command line arguments
parser = argparse.ArgumentParser(description="ARM FS simulation with customizable cache hierarchy")
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
args = parser.parse_args()
octopus_out = args.octopus_out or m5.options.outdir

from gem5.components.cachehierarchies.classic.no_cache import NoCache

if args.classic:
    cache_hierarchy = Gem5BaseCacheHierarchy(args.octopus_config, octopus_out)
else:
    cache_hierarchy = OctopusCacheHierarchy(
        args.octopus_config, octopus_out, extra_params=args.octopus_param
    )
# cache_hierarchy = NoCache()
# Memory: Dual Channel DDR4 2400 DRAM device.

memory = DualChannelDDR4_2400(size="8GiB")

# Here we setup the processor. We use a simple TIMING processor. The config
# script was also tested with ATOMIC processor.

# processor = SimpleProcessor(cpu_type=CPUTypes.ATOMIC, num_cores=4, isa=ISA.ARM)
processor = SimpleProcessor(cpu_type=CPUTypes.O3, num_cores=4, isa=ISA.ARM)
if args.cache_ports:
    _loads, _stores = (int(x) for x in args.cache_ports.split(","))
    for _c in processor.get_cores():
        _c.core.cacheLoadPorts = _loads
        _c.core.cacheStorePorts = _stores
# processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, num_cores=4, isa=ISA.ARM)
# processor = SimpleSwitchableProcessor(
#    starting_core_type=CPUTypes.ATOMIC,
#    switch_core_type=CPUTypes.O3,
#    isa=ISA.ARM,
#    num_cores=4,
#    )

# The ArmBoard requires a `release` to be specified. This adds all the
# extensions or features to the system. We are setting this to Armv8
# (ArmDefaultRelease) in this example config script.
release = ArmDefaultRelease()

# platform = VExpress_GEM5_V1()
platform = VExpress_GEM5_Foundation()

# Here we setup the board. The ArmBoard allows for Full-System ARM simulations.

board = ArmBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
    release=release,
    platform=platform,
)

# Here we set a full system workload. The "arm64-ubuntu-20.04-boot" boots
# Ubuntu 20.04.

# board.set_workload(Workload("arm64-ubuntu-20.04-boot"))
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
board.set_kernel_disk_workload(
    # kernel=KernelResource("/workspaces/gem5/resource/vmlinux"),
    # disk_image=DiskImageResource("/workspaces/gem5/resource/ubuntu-18.04.img"),
    # bootloader=BootloaderResource("/workspaces/gem5/resource/boot_emm.arm64-20220707"),
    kernel=KernelResource("/home/guotong/gem5_resource/arm64-linux-kernel-5.15.180"),
    disk_image=DiskImageResource("/home/guotong/gem5_resource/arm-ubuntu-22.04.img"),
    bootloader=BootloaderResource("/home/guotong/gem5_resource/boot_foundation.arm64-20220707"),
    
    readfile="/home/guotong/gem5_resource/ov2slam_octopus.rcS",
    # readfile="/home/guotong/gem5_resource/testarm.rcS",
    kernel_args=kernel_cmd,
    # checkpoint=CheckpointResource("/home/guotong/gem5_resource/test_start"),
    checkpoint=CheckpointResource("/home/guotong/gem5_resource/ov2_start"),
    # checkpoint=CheckpointResource("/workspaces/gem5/resource/ov2_3s_end"),
)

def handle_workend():
    print("Dump stats at the end of the ROI!")
    m5.stats.dump()
    # simulator.save_checkpoint("/workspaces/gem5/resource/ov2_3s_end_ref")
    # processor.switch()
    # cache_hierarchy.setLogEn(False)
    yield True

# def handle_workbegin():
#     print("Resetting stats at the start of ROI!")
#     m5.stats.reset()
#     # flags["ExecAll"].enable()
#     # flags["LLSC"].enable()
#     # flags["CacheAll"].enable()
#     # flags["OctopusFull"].enable()
#     # cache_hierarchy.setLogEn(True)
#     processor.switch()
#     yield False

def handle_workbegin():
    print("Resetting stats at the start of ROI!")
    m5.stats.reset()
    # processor.switch()
    simulator.save_checkpoint("/home/guotong/gem5_resource/ov2_start")
    yield True

def exit_event_handler():
    print('first exit event: Kernel booted')
    yield False
    print('second exit event: In after boot')
    yield False
    print('third exit event: After run script')
    yield True


simulator = Simulator(board=board,
    on_exit_event={
            ExitEvent.WORKBEGIN: handle_workbegin(),
            ExitEvent.WORKEND: handle_workend(),
            ExitEvent.EXIT: exit_event_handler(),
        },
    )

# Once the system successfully boots, it encounters an
# `m5_exit instruction encountered`. We stop the simulation then. When the
# simulation has ended you may inspect `m5out/board.terminal` to see
# the stdout.
print(atp_files)
if args.max_ticks:
    simulator.run(max_ticks=args.max_ticks)
else:
    simulator.run()
