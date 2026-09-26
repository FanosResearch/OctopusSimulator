from m5.objects.ClockedObject import ClockedObject
from m5.util.pybind import PyBindMethod
from m5.params import *
from m5.proxy import *


class Octopus(ClockedObject):
    type = "Octopus"
    cxx_header = "gem5/octopus.hh"
    cxx_class = "gem5::Octopus"

    cxx_exports = [PyBindMethod("setOctLoggerEn")]

    # Vector port example. Both the instruction and data ports connect to this
    # port which is automatically split out into two ports.
    cpu_side = VectorResponsePort("CPU side port, receives requests")
    mem_side = RequestPort("Memory side port, sends requests")

    system = Param.System(Parent.any, "The system this cache is part of")

    cache_id = Param.Int(
        0, "Id of the Octopus L1 (and of its ExternalCPU) this bridge drives"
    )

    # How far Octopus is advanced per gem5 clock cycle. This is the core/cache
    # period of the Octopus configuration (cpu[*].m_clk_period in the system
    # CSV), so one Octopus cache cycle maps to one gem5 CPU cycle -- correct
    # for an L1, which runs in the core clock domain. Do not express this as a
    # step count: ClockManager::clkStep() advances to the next scheduled
    # event, so the distance per step depends on the finest registered period.
    octopus_cycle_ns = Param.Unsigned(
        100, "Octopus ns to advance per gem5 cycle (core period in the CSV)."
    )

    # Octopus is configured from CSV presets: system_name selects the system
    # class, config_name the file under configuration/SystemConfigurations,
    # and cl_params are 'name(type)=value' overrides applied on top of it,
    # e.g. "workload_path(s)=/path/to/out" or "cache_controller[*].num_mshr(i)=8".
    system_name = Param.String("MultiCoreSystem", "Octopus system class")
    config_name = Param.String(
        "MultiCoreSystem_gem5",
        "CSV under configuration/SystemConfigurations describing the system",
    )
    cl_params = VectorParam.String(
        [], "Overrides 'name(type)=value' applied on top of the CSV"
    )

    def setOctLoggerEn(self, flag):
        self.getCCObject().setOctLoggerEn(flag)
