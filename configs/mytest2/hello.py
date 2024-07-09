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
from gem5.components.cachehierarchies.ruby.mesi_two_level_cache_hierarchy import MESITwoLevelCacheHierarchy
from gem5.utils.requires import requires
from gem5.coherence_protocol import CoherenceProtocol

import os
import shutil
import pathlib
import argparse

# requires(
#     isa_required=ISA.X86,
#     coherence_protocol_required=CoherenceProtocol.MESI_TWO_LEVEL,
# )

parser = argparse.ArgumentParser()
parser.add_argument("-c", type=int, help="number of cores")
parser.add_argument("-b", type=str, help="benchmark name")
parser.add_argument("-f", type=str, help="configuration file path")
parser.add_argument("-m", type=str, help="cache hierarchy")
args = parser.parse_args()

BM_name = args.b
num_cores = args.c
hierarchy_name = args.m

# Obtain the components.
# cache_hierarchy = NoCache()
SimpleCache.config_file_path = args.f

if hierarchy_name == "Octopus":
    cache_hierarchy = UniqueCacheHierarchy()
# cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
#     l1d_size="16KiB", l1i_size="16KiB", l2_size="128KiB"
# )
# cache_hierarchy = SimpleCache(size="1kB")
elif hierarchy_name == "Ruby":
    cache_hierarchy = MESITwoLevelCacheHierarchy(
        l1d_size="16KiB",
        l1d_assoc=2,
        l1i_size="16KiB",
        l1i_assoc=2,
        l2_size="256KiB",
        l2_assoc=32,
        num_l2_banks=1,
    )
elif hierarchy_name == "NoCache":
    cache_hierarchy = NoCache()


memory = SingleChannelDDR3_1600("4GiB")
# processor = SimpleProcessor(cpu_type=CPUTypes.ATOMIC, isa=ISA.X86, num_cores=num_cores)
processor = SimpleProcessor(cpu_type=CPUTypes.O3, isa=ISA.X86, num_cores=num_cores)

# Add them to the board.
board = SimpleBoard(
    clk_freq="1GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy
)

# Set the workload.
# binary = Resource("x86-hello64-static")
# binary = CustomResource("/home/gem5/gem5_new/configs/mytest/matrix-multiply")
# binary = CustomResource("/home/gem5/gem5_new/tests/test-progs/threads/bin/x86/linux/threads")
# binary = CustomResource("/home/gem5/splash/Splash-3/codes//apps/radiosity/RADIOSITY")
# args = ["-p 4", "-ae 5000", "-bf 0.1", "-en 0.05", "-room", "-batch"];
# binary = CustomResource("/home/gem5/splash/Splash-3/codes/kernels/fft/FFT")
# args = ["-p4", "-m16"];
# binary = CustomResource("/home/gem5/splash/Splash-3/codes/kernels/lu/contiguous_blocks/LU")
# args = ["-p4", "-n512"];
# binary = CustomResource("/home/gem5/splash/Splash-3/codes/kernels/lu/non_contiguous_blocks/LU")
# args = ["-p4", "-n512"];
# binary = CustomResource("/home/gem5/splash/Splash-3/codes/kernels/radix/RADIX")
# args = ["-p4", "-n1048576"];

if BM_name == "FFT":
    binary = CustomResource("/home/gem5/splash/splash2/codes/kernels/fft/FFT") #done (gem5 has an error with m14)
    args = ["-p" + str(num_cores), "-m16"] #octo = 170m40s(m16) 10m21(m12) 12m34(m12 + 4steps) 10m23(m12 + 2steps) 7m29(m12, O3 1step) 7585.15 (m16, O3, 330756kb) 6845 (m16, O3, 328936Kb), gem5 = 104m5s(m16) 6m3(m12) 4m1(m12, O3) 5730.58s (m16, O3, 742160kb)
elif BM_name == "LU":
    # binary = CustomResource("/home/gem5/splash/splash2/codes/kernels/lu/contiguous_blocks/LU") #done
    # args = ["-p" + str(num_cores)]
    binary = CustomResource("/home/gem5/splash/splash2/codes/kernels/lu/non_contiguous_blocks/LU") #done
    args = ["-p" + str(num_cores), "-n400"] #OCto= O3 10981s 431532Kb, gem5 = O3 7620s 732232Kb, O3 7930s 732368KB new
elif BM_name == "RADIX":
    binary = CustomResource("/home/gem5/splash/splash2/codes/kernels/radix/RADIX") #done
    args = ["-p" + str(num_cores)] #octo = 54m7, O3 2443s 232316kb, gem5 = 42m2, O3 2538s 736868kb
elif BM_name == "CHOLESKY":    
    binary = CustomResource("/home/gem5/splash/splash2/codes/kernels/cholesky/CHOLESKY") #failed on gem5
    args = ["-p" + str(num_cores), "/home/gem5/splash/splash2/codes/kernels/cholesky/inputs/tk23.O"]
elif BM_name == "OCEAN":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/ocean/contiguous_partitions/OCEAN") #octo = 131m26s(n66), gem5 = 106m0(n130)
    args = ["-p" + str(num_cores)]
# binary = CustomResource("/home/gem5/splash/splash2/codes/apps/ocean/non_contiguous_partitions/OCEAN")
# args = ["-p4"]
elif BM_name == "RADIOSITY":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/radiosity/RADIOSITY")
    args = ["-p", str(num_cores), "-batch"] #octo =893m55, O3 34297s 584736Kb, gem5 = 431m40, O3 20612s 791184kb
elif BM_name == "RAYTRACE":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/raytrace/RAYTRACE") #Octo = 435m22s, Gem5 = failed
    args = ["-p" + str(num_cores), "-m64", "/home/gem5/splash/splash2/codes/apps/raytrace/inputs/car.env"]
elif BM_name == "BARNS":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/barnes/BARNES") #octo = 92m8, gem5 = 67m7, (O3 3320s 729296Kb 2xcachesize)
    args = []
elif BM_name == "FMM":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/fmm/FMM") #octo = 68m53, gem5 = 44m556, (O3 3106s 731216Kb new)
    args = []
elif BM_name == "WATER":
    binary = CustomResource("/home/gem5/splash/splash2/codes/apps/water-nsquared/WATER-NSQUARED")
    args = [] #octo = 1958m4, gem5 = 1334m19
# binary = CustomResource("/home/gem5/splash/splash2/codes/apps/water-spatial/WATER-SPATIAL")
# args = []
elif BM_name == "Threads":
    binary = CustomResource("/home/gem5/gem5_new/tests/test-progs/threads/bin/x86/linux/threads")
    # args = ["10"]
    args = ["409600"]

board.set_se_binary_workload(
    binary = binary,
    arguments = args,
)

# Setup the Simulator and run the simulation.
simulator = Simulator(board=board)
simulator.run()


source_folder = "/home/gem5/gem5_new/.vscode/generatedBMs/"
destination_folder = "/media/sf_Gem5_vm_sharedfolder/generatedBMs/" + str(num_cores) + "Cores/" + BM_name + "/"
pathlib.Path(destination_folder).mkdir(parents=True, exist_ok=True)

# fetch all files
for file_name in os.listdir(source_folder):
    # construct full file path
    source = source_folder + file_name
    destination = destination_folder + file_name
    # move only files
    if os.path.isfile(source):
        shutil.move(source, destination)