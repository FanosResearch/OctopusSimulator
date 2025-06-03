from m5.objects import *
from m5.objects import (
    Port,
    SystemXBar,
    AddrRange,
    ProfileGen,
    Octopus,
)

from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.mmu_cache import MMUCache
from gem5.isas import ISA

class OctopusCacheHierarchy(AbstractClassicCacheHierarchy):
    def __init__(self,xml_path,output_path, atp_files = None) -> None:
        AbstractClassicCacheHierarchy.__init__(self=self)

        self._useATP = False
        self._xmlPath = xml_path
        self._outputPath = output_path
        
        self.membus = SystemXBar(width=64)
        if atp_files is not None:
            self._useATP = True
            self.atp_adaptor = ProfileGen(config_files=atp_files, exit_when_done=False, trace_atp=False, init_only=True)

    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        tmp = 0
        self.l1i_caches = [
            Octopus(
                req_fifo_size=256,
                cache_id=tmp+i,
                config_file_path=self._xmlPath,
                output_logs_path=self._outputPath,
            )
            for i in range(board.get_processor().get_num_cores())
        ]

        tmp = board.get_processor().get_num_cores()
        self.l1d_caches = [
            Octopus(
                req_fifo_size=256,
                cache_id=tmp+i,
                config_file_path=self._xmlPath,
                output_logs_path=self._outputPath,
            )
            for i in range(board.get_processor().get_num_cores())
        ]

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
            tmp = board.get_processor().get_num_cores() * 2
            self.atp_caches = [
                Octopus(
                    req_fifo_size=256,
                    cache_id=tmp+i,
                    config_file_path=self._xmlPath,
                    output_logs_path=self._outputPath,
                )
                for _ in range(len(self.atp_adaptor.config_files))
            ]
            for i in range(len(self.atp_adaptor.config_files)):
                self.atp_adaptor.port = self.atp_caches[i].cpu_side
                self.atp_caches[i].mem_side = self.membus.cpu_side_ports
                i+=1

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