from m5.objects import *
from m5.objects import (
    Port,
    SystemXBar,
    AddrRange,
    Octopus,
)

from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.mmu_cache import MMUCache
from gem5.isas import ISA


class OctopusCacheHierarchy(AbstractClassicCacheHierarchy):
    """
    One Octopus bridge per L1. The Octopus system itself is described by the
    CSV preset `config_name` (configuration/SystemConfigurations/<name>.csv);
    it must declare cpu_type=ExternalCPU and one core per bridge, with the
    instruction caches at ids 0..n-1 and the data caches at n..2n-1.
    """

    def __init__(self, config_name, output_path, atp_files=None, extra_params=None) -> None:
        AbstractClassicCacheHierarchy.__init__(self=self)
        self._useATP = False
        self._configName = config_name
        self._outputPath = output_path
        # Octopus 'name(type)=value' overrides applied on top of the preset,
        # e.g. "bus[0].interconnect_controller.m_response_latency(i)=1".
        # Every bridge must carry the same list: only the first one built
        # constructs the Octopus system.
        self._extraParams = list(extra_params or [])
        self.membus = SystemXBar(width=64)
        if atp_files is not None:
            self._useATP = True

    def _bridge(self, cache_id):
        return Octopus(
            cache_id=cache_id,
            system_name="MultiCoreSystem",
            config_name=self._configName,
            # The Logger writes its per-core reports under <workload_path>/newLogger.
            cl_params=[f"workload_path(s)={self._outputPath}"] + self._extraParams,
        )

    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        n = board.get_processor().get_num_cores()
        self.l1i_caches = [self._bridge(i) for i in range(n)]
        self.l1d_caches = [self._bridge(n + i) for i in range(n)]

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            cpu.connect_icache(self.l1i_caches[i].cpu_side)
            cpu.connect_dcache(self.l1d_caches[i].cpu_side)

            self.l1i_caches[i].mem_side = self.membus.cpu_side_ports
            self.l1d_caches[i].mem_side = self.membus.cpu_side_ports

            cpu.connect_walker_ports(
                self.membus.cpu_side_ports, self.membus.cpu_side_ports
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        if self._useATP:
            base = 2 * n
            self.atp_caches = [
                self._bridge(base + i)
                for i in range(len(self.atp_adaptor.config_files))
            ]
            for i in range(len(self.atp_adaptor.config_files)):
                self.atp_adaptor.port = self.atp_caches[i].cpu_side
                self.atp_caches[i].mem_side = self.membus.cpu_side_ports

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        """Create a cache for coherent I/O connections"""
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1kB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()

    def setLogEn(self, flag):
        self.l1d_caches[0].setOctLoggerEn(flag)
