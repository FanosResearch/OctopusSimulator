import m5
# m5.util.addToPath("../../")

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
    DiskImageResource
)
import argparse

from octopus_cache_hierarchy import OctopusCacheHierarchy

requires(isa_required=ISA.ARM)

import os
def get_atp_files(atp_files_path):
    atp_files = set()
    for path in atp_files_path:
        for dname, _, fnames in os.walk(path):
            atp_files.update([os.path.join(dname, fname) for fname in fnames])
    return list(atp_files)
# Here we setup the parameters of the l1 and l2 caches.
atp_files = get_atp_files(["/workspaces/gem5/resource/atp_files"])

# Add argument parser for command line arguments
parser = argparse.ArgumentParser(description="ARM FS simulation with customizable cache hierarchy")
parser.add_argument(
    "--octopus-xml", 
    type=str,
    default="/workspaces/OctopusSimulator/test/arm_challenge/tc_FR_1C_1B.xml",
    help="Path to the CMSpec XML file for cache configuration"
)
args = parser.parse_args()

# cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
#     l1d_size="16kB", l1i_size="16kB", l2_size="256kB"
# )
# cache_hierarchy = ATPCacheHierarchy(
#     l1d_size="16kB", l1i_size="16kB", l2_size="256kB"
# )
cache_hierarchy = OctopusCacheHierarchy(args.octopus_xml, "/workspaces/OctopusSimulator/log/")

# Memory: Dual Channel DDR4 2400 DRAM device.

memory = DualChannelDDR4_2400(size="8GiB")

# Here we setup the processor. We use a simple TIMING processor. The config
# script was also tested with ATOMIC processor.

#processor = SimpleProcessor(cpu_type=CPUTypes.O3, num_cores=4, isa=ISA.ARM)
processor = SimpleSwitchableProcessor(
    starting_core_type=CPUTypes.ATOMIC,
    switch_core_type=CPUTypes.O3,
    isa=ISA.ARM,
    num_cores=4,
    )
# The ArmBoard requires a `release` to be specified. This adds all the
# extensions or features to the system. We are setting this to Armv8
# (ArmDefaultRelease) in this example config script.
release = ArmDefaultRelease()

platform = VExpress_GEM5_V1()

# Here we setup the board. The ArmBoard allows for Full-System ARM simulations.

board = ArmBoard(
    clk_freq="1.4GHz",
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
        "root=/dev/vda1",
        "rw",
        "init=/sbin/init",
        "vmalloc=768MB",
    ]
from pathlib import Path
board.set_kernel_disk_workload(
    kernel=KernelResource("/workspaces/gem5/resource/vmlinux"),
    disk_image=DiskImageResource("/workspaces/gem5/resource/ubuntu-18.04.img"),
    bootloader=BootloaderResource("/workspaces/gem5/system/arm/bootloader/arm64/boot.arm64"),
    readfile="/workspaces/gem5/resource/ov2slam_octopus.rcS",
    # readfile="/workspaces/gem5/resource/testarm.rcS",
    kernel_args=kernel_cmd,
    # checkpoint=checkpoint_path,
)

def handle_workend():
    print("Dump stats at the end of the ROI!")
    m5.stats.dump()
    cache_hierarchy.setLogEn(False)
    yield True

def handle_workbegin():
    print("Resetting stats at the start of ROI!")
    m5.stats.reset()
    cache_hierarchy.setLogEn(True)
    processor.switch()
    yield False

simulator = Simulator(board=board,
    on_exit_event={
            ExitEvent.WORKBEGIN: handle_workbegin(),
            ExitEvent.WORKEND: handle_workend(),
        },
    )

# Once the system successfully boots, it encounters an
# `m5_exit instruction encountered`. We stop the simulation then. When the
# simulation has ended you may inspect `m5out/board.terminal` to see
# the stdout.
print(atp_files)
simulator.run()
