from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import PrivateL1PrivateL2CacheHierarchy
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.resources.resource import Resource
from gem5.resources.resource import CustomResource
from gem5.simulate.simulator import Simulator
from gem5.isas import ISA

from m5.objects import *

from unique_cache_hierarchy_complete import UniqueCacheHierarchy


# Obtain the components.
# cache_hierarchy = UniqueCacheHierarchy()
cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
    l1d_size="32KiB", l1i_size="32KiB", l2_size="64KiB"
)
# cache_hierarchy = SimpleCache(size="1kB")

memory = SingleChannelDDR3_1600("1GiB")
processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=4)

# Add them to the board.
board = SimpleBoard(
    clk_freq="3GHz", processor=processor, memory=memory, cache_hierarchy=cache_hierarchy
)

# Set the workload.
# binary = Resource("x86-hello64-static")
# binary = CustomResource("/home/gem5/gem5_new/configs/mytest/matrix-multiply")
binary = CustomResource("/home/gem5/gem5_new/tests/test-progs/threads/bin/x86/linux/threads")

board.set_se_binary_workload(binary)

# Setup the Simulator and run the simulation.
simulator = Simulator(board=board)
simulator.run()