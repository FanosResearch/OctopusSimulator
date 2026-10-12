# SE-mode counterpart of fs_arm.py: the same Octopus (or classic baseline)
# cache hierarchy, O3 cores and DDR4 memory, but driving an aarch64 binary
# loaded straight from the host instead of a booted Linux image. No kernel,
# no disk image, no checkpoint: a run starts in seconds, which makes this the
# quick check for bridge / protocol changes before an FS run.
#
#   gem5.opt -re -d <outdir> se_arm.py [--binary PATH] [--classic] \
#       [--octopus-param 'name(type)=value'] [-- <arguments for the binary>]
#
# The default binary is gem5/se_test/se_test-static in this tree (see the Makefile
# there). It brackets its work with m5_work_begin / m5_work_end, which arrive
# here as the WORKBEGIN / WORKEND exit events: stats are reset at the first and
# dumped at the second, so stats.txt covers the ROI only, as the FS runs do
# with their resetstats / dumpstats markers.

import argparse
import os

import m5
from m5.core import setInterpDir
from m5.objects import RedirectPath

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import (
    ExitEvent,
    Simulator,
)
from gem5.utils.requires import requires

from octopus_cache_hierarchy import OctopusCacheHierarchy
from gem5_base_cache_hierarchy import Gem5BaseCacheHierarchy

requires(isa_required=ISA.ARM)

SE_TEST_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "se_test")

parser = argparse.ArgumentParser(
    description="ARM SE simulation with the Octopus or classic cache hierarchy"
)
parser.add_argument(
    "--binary",
    type=str,
    default=os.path.join(SE_TEST_DIR, "se_test-static"),
    help="aarch64 ELF to run (default: the se_test static build)",
)
parser.add_argument(
    "args",
    nargs="*",
    help="Arguments passed to the binary; put them after '--'. "
         "se_test takes [threads] [KiB per thread] [iterations]",
)
parser.add_argument(
    "--workload-type",
    choices=["static", "dynamic"],
    default="static",
    help="A dynamic binary needs the aarch64 loader and libraries; they are "
         "taken from --sysroot (default: static)",
)
parser.add_argument(
    "--sysroot",
    type=str,
    default="/usr/aarch64-linux-gnu",
    help="Directory holding the aarch64 loader and libraries for a dynamic "
         "binary (default: the cross toolchain's sysroot; a mounted gem5 disk "
         "image works too)",
)
parser.add_argument(
    "--num-cores",
    type=int,
    default=4,
    help="O3 cores. The Octopus preset must declare 2 x this many L1s "
         "(MultiCoreSystem_gem5: num_cores 8, i.e. up to 4 cores)",
)
parser.add_argument(
    "--cpu-type",
    choices=["o3", "timing"],
    default="o3",
    help="Core model (default o3, as in fs_arm.py; timing isolates O3 issues)",
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
args = parser.parse_args()
octopus_out = args.octopus_out or m5.options.outdir

if args.classic:
    cache_hierarchy = Gem5BaseCacheHierarchy(args.octopus_config, octopus_out)
else:
    cache_hierarchy = OctopusCacheHierarchy(
        args.octopus_config, octopus_out, extra_params=args.octopus_param
    )

# Same memory as the FS configuration.
memory = DualChannelDDR4_2400(size="8GiB")

cpu_type = {"o3": CPUTypes.O3, "timing": CPUTypes.TIMING}[args.cpu_type]
processor = SimpleProcessor(
    cpu_type=cpu_type, num_cores=args.num_cores, isa=ISA.ARM
)
if args.cache_ports:
    _loads, _stores = (int(x) for x in args.cache_ports.split(","))
    for _c in processor.get_cores():
        _c.core.cacheLoadPorts = _loads
        _c.core.cacheStorePorts = _stores

# SimpleBoard is the SE-mode board: no platform, no I/O, so the hierarchy's
# iocache path is skipped (has_coherent_io() is False).
board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

if args.workload_type == "dynamic":
    # The ELF interpreter is resolved against this directory and the library
    # paths the loader opens are redirected into it.
    print(f"Dynamic binary: loader and libraries from {args.sysroot}")
    setInterpDir(args.sysroot)
    board.redirect_paths = [
        RedirectPath(app_path=p, host_paths=[args.sysroot + p])
        for p in ("/lib", "/usr/lib", "/etc")
    ]

# One process; a threaded binary spreads over the cores through clone().
board.set_se_binary_workload(
    binary=BinaryResource(local_path=args.binary),
    arguments=args.args,
)


def handle_workbegin():
    print("Resetting stats at the start of ROI!")
    m5.stats.reset()
    yield False


def handle_workend():
    print("Dump stats at the end of the ROI!")
    m5.stats.dump()
    yield False


def exit_event_handler():
    # SE mode: "exiting with last active thread context".
    print("exit event: the binary has exited")
    yield True


simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.WORKBEGIN: handle_workbegin(),
        ExitEvent.WORKEND: handle_workend(),
        ExitEvent.EXIT: exit_event_handler(),
    },
)

print(f"Running {args.binary} {' '.join(args.args)} on {args.num_cores} "
      f"{args.cpu_type} core(s), {'classic' if args.classic else 'Octopus'} "
      f"caches, preset {args.octopus_config}")
if args.max_ticks:
    simulator.run(max_ticks=args.max_ticks)
else:
    simulator.run()
print(f"Simulation ended: {simulator.get_last_exit_event_cause()} "
      f"at tick {simulator.get_current_tick()}")
