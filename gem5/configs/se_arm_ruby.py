# Ruby counterpart of se_arm.py for the gem5 Ruby/Garnet versus gem5 + Octopus comparison
# (docs/gem5_SPLASH_Garnet_vs_Octopus.md). Same board, O3 cores, 3 GHz clock and ROI handling as
# se_arm.py; the cache hierarchy is Ruby MESI_Two_Level (private L1I/L1D per core, shared banked L2
# that holds the directory state, one Directory controller per memory channel) on a 4x4 mesh:
#
#   router t: core t's L1 controller and L2 bank t;  routers MEM_ROUTERS: the two Directory
#   controllers (one per DRAM channel), as Octopus's memory controllers 100 and 101.
#
# The network is Garnet 3.0 (--network garnet, the default) or the Ruby SimpleNetwork
# (--network simple) on the same topology and routing (XY).
#
#   gem5.opt -re -d <outdir> se_arm_ruby.py --binary PATH [--num-cores 16] [--network garnet]
#       [--stdin FILE] [--dram mcsim_match|ddr4_2400] [-- <arguments for the binary>]
#
# Needs a gem5 built with RUBY and PROTOCOL MESI_Two_Level (build/ARM_MESI2 in the gem5-ruby
# worktree). WORKBEGIN dumps the pre-ROI statistics and resets them, WORKEND dumps them: stats.txt
# section 1 is the part before the ROI, section 2 the ROI, section 3 the rest up to the exit.

import argparse
import math
import os
import sys

import m5
from m5.objects import (
    GarnetExtLink,
    GarnetIntLink,
    GarnetNetwork,
    GarnetNetworkInterface,
    GarnetRouter,
    LRURP,
    RubyPortProxy,
    RubySequencer,
    RubySystem,
    SimpleExtLink,
    SimpleIntLink,
    SimpleNetwork,
    Switch,
)

from gem5.coherence_protocol import CoherenceProtocol
from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.abstract_cache_hierarchy import (
    AbstractCacheHierarchy,
)
from gem5.components.cachehierarchies.ruby.abstract_ruby_cache_hierarchy import (
    AbstractRubyCacheHierarchy,
)
from gem5.components.cachehierarchies.ruby.caches.mesi_two_level.directory import (
    Directory,
)
from gem5.components.cachehierarchies.ruby.caches.mesi_two_level.l1_cache import (
    L1Cache,
)
from gem5.components.cachehierarchies.ruby.caches.mesi_two_level.l2_cache import (
    L2Cache,
)
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource, FileResource
from gem5.simulate.simulator import ExitEvent, Simulator
from gem5.utils.override import overrides
from gem5.utils.requires import requires

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from matched_dram import DRAM_MODELS, make_memory

requires(
    isa_required=ISA.ARM,
    coherence_protocol_required=CoherenceProtocol.MESI_TWO_LEVEL,
)

MEM_ROUTERS = [4, 11]   # as interconnect.node_map[100/101] in MultiCoreSystem_gem5_mesh_ruby.csv


class MeshMESITwoLevel(AbstractRubyCacheHierarchy):
    """MESI_Two_Level with one L1 controller and one L2 bank per mesh router."""

    def __init__(self, a):
        AbstractRubyCacheHierarchy.__init__(self=self)
        self._a = a

    @overrides(AbstractCacheHierarchy)
    def get_coherence_protocol(self):
        return CoherenceProtocol.MESI_TWO_LEVEL

    def _network(self):
        a = self._a
        if a.network == "garnet":
            net = GarnetNetwork(
                ruby_system=self.ruby_system,
                topology="Mesh_XY",
                num_rows=a.mesh_rows,
                ni_flit_size=a.flit_bytes,
                vcs_per_vnet=a.vcs_per_vnet,
                buffers_per_data_vc=a.vc_depth,
                buffers_per_ctrl_vc=a.ctrl_vc_depth,
                routing_algorithm=1,   # XY
            )
        else:
            net = SimpleNetwork(ruby_system=self.ruby_system, topology="Mesh_XY")
        net.number_of_virtual_networks = 3
        return net

    def _connect(self, net, placed):
        """placed: (controller, router id). XY mesh as configs/topologies/Mesh_XY.py."""
        a = self._a
        garnet = a.network == "garnet"
        k = a.mesh_rows
        n = k * k
        if garnet:
            routers = [GarnetRouter(router_id=i, latency=a.router_latency) for i in range(n)]
        else:
            routers = [Switch(router_id=i, latency=a.router_latency) for i in range(n)]
        net.routers = routers
        Ext = GarnetExtLink if garnet else SimpleExtLink
        Int = GarnetIntLink if garnet else SimpleIntLink
        extra = {} if garnet else {"bandwidth_factor": a.flit_bytes}
        link_id = 0
        ext_links = []
        for ctrl, r in placed:
            ext_links.append(Ext(link_id=link_id, ext_node=ctrl, int_node=routers[r],
                                 latency=a.link_latency, **extra))
            link_id += 1
        net.ext_links = ext_links
        int_links = []

        def link(s, d, so, di, w):
            nonlocal link_id
            int_links.append(Int(link_id=link_id, src_node=routers[s], dst_node=routers[d],
                                 src_outport=so, dst_inport=di, latency=a.link_latency,
                                 weight=w, **extra))
            link_id += 1

        for row in range(k):
            for col in range(k - 1):
                link(col + row * k, col + 1 + row * k, "East", "West", 1)
                link(col + 1 + row * k, col + row * k, "West", "East", 1)
        for col in range(k):
            for row in range(k - 1):
                link(col + row * k, col + (row + 1) * k, "North", "South", 2)
                link(col + (row + 1) * k, col + row * k, "South", "North", 2)
        net.int_links = int_links
        if garnet:
            net.netifs = [GarnetNetworkInterface(id=i) for i in range(len(ext_links))]
        else:
            net.netifs = []
            net.setup_buffers()

    def incorporate_cache(self, board: AbstractBoard) -> None:
        a = self._a
        line = board.get_cache_line_size()
        n_cores = board.get_processor().get_num_cores()
        n_routers = a.mesh_rows * a.mesh_rows
        n_banks = n_routers
        assert n_cores <= n_routers, "one core per router"

        self.ruby_system = RubySystem()
        self.ruby_system.number_of_virtual_networks = 3
        self.ruby_system.network = self._network()
        net = self.ruby_system.network

        self._l1_controllers = []
        for i, core in enumerate(board.get_processor().get_cores()):
            c = L1Cache(a.l1_size, a.l1_assoc, a.l1_size, a.l1_assoc, net, core, n_banks,
                        line, board.processor.get_isa(), board.get_clock_domain())
            # The stdlib L1Cache passes start_index_bit=cache_line_size (64, a byte count)
            # instead of log2(64) = 6: every line maps to set 0 and the 32 KiB L1 holds 8 lines.
            c.L1Icache.start_index_bit = int(math.log2(line))
            c.L1Dcache.start_index_bit = int(math.log2(line))
            c.L1Icache.replacement_policy = LRURP()
            c.L1Dcache.replacement_policy = LRURP()
            c.mandatory_queue_latency = a.l1_hit_latency
            c.l1_request_latency = a.l1_request_latency
            c.l1_response_latency = a.l1_response_latency
            c.to_l2_latency = 1
            c.sequencer = RubySequencer(version=i, dcache=c.L1Dcache, clk_domain=c.clk_domain,
                                        ruby_system=self.ruby_system,
                                        max_outstanding_requests=a.l1_mshrs)
            c.ruby_system = self.ruby_system
            core.connect_icache(c.sequencer.in_ports)
            core.connect_dcache(c.sequencer.in_ports)
            core.connect_walker_ports(c.sequencer.in_ports, c.sequencer.in_ports)
            core.connect_interrupt()
            self._l1_controllers.append(c)

        self._l2_controllers = []
        for b in range(n_banks):
            c = L2Cache(a.l2_bank_size, a.l2_assoc, net, n_banks, line)
            c.L2cache.replacement_policy = LRURP()
            c.l2_request_latency = a.l2_request_latency
            c.l2_response_latency = a.l2_response_latency
            c.to_l1_latency = 1
            c.ruby_system = self.ruby_system
            self._l2_controllers.append(c)

        self._directory_controllers = []
        for rng, port in board.get_mem_ports():
            d = Directory(net, line, rng, port)
            d.ruby_system = self.ruby_system
            self._directory_controllers.append(d)
        assert len(self._directory_controllers) <= len(MEM_ROUTERS)

        self.ruby_system.num_of_sequencers = len(self._l1_controllers)
        self.ruby_system.l1_controllers = self._l1_controllers
        self.ruby_system.l2_controllers = self._l2_controllers
        self.ruby_system.directory_controllers = self._directory_controllers

        placed = [(c, i) for i, c in enumerate(self._l1_controllers)]
        placed += [(c, b) for b, c in enumerate(self._l2_controllers)]
        placed += [(d, MEM_ROUTERS[j]) for j, d in enumerate(self._directory_controllers)]
        self._connect(net, placed)

        self.ruby_system.sys_port_proxy = RubyPortProxy(ruby_system=self.ruby_system)
        board.connect_system_port(self.ruby_system.sys_port_proxy.in_ports)

    @overrides(AbstractRubyCacheHierarchy)
    def _reset_version_numbers(self):
        Directory._version = 0
        L1Cache._version = 0
        L2Cache._version = 0


parser = argparse.ArgumentParser(description="ARM SE simulation, Ruby MESI_Two_Level on a mesh")
parser.add_argument("--binary", required=True, help="aarch64 static ELF")
parser.add_argument("args", nargs="*", help="arguments of the binary, after '--'")
parser.add_argument("--stdin", default=None, help="file given to the binary as stdin")
parser.add_argument("--num-cores", type=int, default=16)
parser.add_argument("--cpu-type", choices=["o3", "timing"], default="o3")
parser.add_argument("--network", choices=["garnet", "simple"], default="garnet")
parser.add_argument("--mesh-rows", type=int, default=4)
parser.add_argument("--dram", choices=DRAM_MODELS, default="mcsim_match",
                    help="gem5 DRAM model (matched_dram.py); default: timed as Octopus's MCsim")
g = parser.add_argument_group("matched parameters (defaults: docs/gem5_SPLASH_Garnet_vs_Octopus.md)")
g.add_argument("--l1-size", default="32KiB")
g.add_argument("--l1-assoc", type=int, default=8)
g.add_argument("--l1-mshrs", type=int, default=16, help="Sequencer max_outstanding_requests")
g.add_argument("--l2-bank-size", default="512KiB")
g.add_argument("--l2-assoc", type=int, default=8)
g.add_argument("--l1-hit-latency", type=int, default=2,
               help="mandatory_queue_latency: Octopus's L1 hit is 2 cycles in its LatencyReport")
g.add_argument("--l1-request-latency", type=int, default=1)
g.add_argument("--l1-response-latency", type=int, default=1)
g.add_argument("--l2-request-latency", type=int, default=1)
g.add_argument("--l2-response-latency", type=int, default=11,
               help="Octopus's LLC data array: 10 cycles plus 1 (LatencyReport L2 Access 11)")
g.add_argument("--router-latency", type=int, default=1)
g.add_argument("--link-latency", type=int, default=1)
g.add_argument("--flit-bytes", type=int, default=16)
g.add_argument("--vcs-per-vnet", type=int, default=4)
g.add_argument("--vc-depth", type=int, default=4, help="Garnet buffers_per_data_vc")
g.add_argument("--ctrl-vc-depth", type=int, default=4,
               help="Garnet buffers_per_ctrl_vc (gem5 default 1; Octopus vc_depth is per class)")
parser.add_argument("--max-ticks", type=int, default=None)
args = parser.parse_args()

memory = make_memory(args.dram, size="8GiB")
cpu_type = {"o3": CPUTypes.O3, "timing": CPUTypes.TIMING}[args.cpu_type]
processor = SimpleProcessor(cpu_type=cpu_type, num_cores=args.num_cores, isa=ISA.ARM)
board = SimpleBoard(clk_freq="3GHz", processor=processor, memory=memory,
                    cache_hierarchy=MeshMESITwoLevel(args))
board.set_se_binary_workload(
    binary=BinaryResource(local_path=args.binary),
    arguments=args.args,
    stdin_file=FileResource(local_path=os.path.abspath(args.stdin)) if args.stdin else None,
)


def handle_workbegin():
    # The pre-ROI part is dumped first (stats.txt section 1), so that whole-run network totals can
    # be compared with Octopus's NetworkStats.csv, which covers the whole run. Section 2 is the ROI.
    print(f"Dump stats before the ROI! tick {m5.curTick()}")
    m5.stats.dump()
    print(f"Resetting stats at the start of ROI! tick {m5.curTick()}")
    m5.stats.reset()
    yield False


def handle_workend():
    print(f"Dump stats at the end of the ROI! tick {m5.curTick()}")
    m5.stats.dump()
    yield False


def exit_event_handler():
    print("exit event: the binary has exited")
    yield True


simulator = Simulator(board=board, on_exit_event={
    ExitEvent.WORKBEGIN: handle_workbegin(),
    ExitEvent.WORKEND: handle_workend(),
    ExitEvent.EXIT: exit_event_handler(),
})
print(f"Running {args.binary} {' '.join(args.args)} on {args.num_cores} {args.cpu_type} core(s), "
      f"Ruby MESI_Two_Level, {args.network} {args.mesh_rows}x{args.mesh_rows} mesh, DRAM {args.dram}")
if args.max_ticks:
    simulator.run(max_ticks=args.max_ticks)
else:
    simulator.run()
print(f"Simulation ended: {simulator.get_last_exit_event_cause()} at tick {simulator.get_current_tick()}")
