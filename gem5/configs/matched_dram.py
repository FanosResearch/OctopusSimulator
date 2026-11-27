# The gem5 main memory of the Ruby/Garnet comparison (docs/gem5_SPLASH_Garnet_vs_Octopus.md).
#
# Octopus times DRAM with MCsim (MCsimInterface: DDR4-2400U, 8Gb x8 devices, one rank per
# controller, FR-FCFS, open page); gem5's DRAM then only stores the bytes. The Ruby and classic
# configurations time DRAM with gem5's own controller, so for a like-for-like comparison they use
# the gem5 DDR4 model with MCsim's timing:
#
#   "mcsim_match"  DDR4_2400_8x8 with one rank per channel, the 2400U speed bin (CL = tRCD = tRP =
#                  18 tCK = 15 ns, tRAS 39 tCK, tWTR_S/L 3/9 tCK) and no static controller
#                  pipeline latency (MCsim adds none: its unloaded row-hit read is 53 to 55 core
#                  cycles, CL + burst), two channels interleaved by 64 B line, as Octopus's two
#                  controllers (mem_interleave_shift 0)
#   "ddr4_2400"    the stock DualChannelDDR4_2400 of se_arm.py / fs_arm.py (two ranks per channel,
#                  CL = tRCD = tRP = 14.16 ns, 10 ns static frontend and 10 ns static backend latency)

from m5.objects import DDR4_2400_8x8

from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.memory.memory import ChanneledMemory

_TCK_NS = 0.833


class DDR4_2400U_1R(DDR4_2400_8x8):
    """DDR4_2400_8x8 timed as MCsim's DDR4-2400U, one rank."""

    ranks_per_channel = 1
    tCL = f"{18 * _TCK_NS:.3f}ns"
    tRCD = f"{18 * _TCK_NS:.3f}ns"
    tRP = f"{18 * _TCK_NS:.3f}ns"
    tRAS = f"{39 * _TCK_NS:.3f}ns"
    tWTR = f"{3 * _TCK_NS:.3f}ns"
    tWTR_L = f"{9 * _TCK_NS:.3f}ns"


DRAM_MODELS = ("mcsim_match", "ddr4_2400")


def make_memory(model: str, size: str = "8GiB"):
    if model == "ddr4_2400":
        return DualChannelDDR4_2400(size=size)
    if model == "mcsim_match":
        mem = ChanneledMemory(DDR4_2400U_1R, 2, 64, size=size)
        for ctrl in mem.get_memory_controllers():
            ctrl.static_frontend_latency = "0ns"
            ctrl.static_backend_latency = "0ns"
        return mem
    raise ValueError(f"unknown DRAM model {model}; choose one of {DRAM_MODELS}")
