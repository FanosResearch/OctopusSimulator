from __future__ import annotations

from dataclasses import dataclass
from typing import Optional
import os
from pathlib import Path

from m5.objects import (
    BadAddr,
    BaseCPU,
    Cache,
    L2XBar,
    SystemXBar,
)
from m5.params import Port

from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.l1dcache import L1DCache
from gem5.components.cachehierarchies.classic.caches.l1icache import L1ICache
from gem5.components.cachehierarchies.classic.caches.l2cache import L2Cache
from gem5.isas import ISA


# L1 hit latency for the classic baseline.
#
# The Octopus XML's dataAccessLatency ("0" for the private cache) is only one
# component of an Octopus L1 hit; the rest is structural (ExternalCPU ->
# interconnect -> controller and back) and does not appear in the XML. Copying
# that 0 into gem5 gave the classic baseline a zero-latency L1, which is not
# what Octopus models. Measured Octopus L1 hit is ~3 cycles (fetch stall of
# 3.3-3.7 cycles per line, of which ~0.4 is the miss share), and a real 3 GHz
# out-of-order core is 3-5.
#
# How gem5 combines the three (src/mem/cache/base.cc):
#   hit  = max(tag, data)            with sequential_access=False (the default)
#   miss = tag (forward) + lower level + response (return path)
# response_latency is not charged on a hit at all.
L1_TAG_LATENCY = 3
L1_DATA_LATENCY = 3
L1_RESPONSE_LATENCY = 1


@dataclass(frozen=True)
class _CacheConfig:
    size_bytes: int
    assoc: int
    latency: int
    mshrs: int

    def size_str(self) -> str:
        return _bytes_to_size_str(self.size_bytes)


def _bytes_to_size_str(size_bytes: int) -> str:
    for unit, factor in ("GiB", 1024**3), ("MiB", 1024**2), ("KiB", 1024):
        if size_bytes % factor == 0:
            return f"{size_bytes // factor}{unit}"
    return f"{size_bytes}B"


def _octopus_root() -> Path:
    # <octopus root>/gem5/configs/this_file.py
    return Path(__file__).resolve().parents[2]


def _read_preset(config_name: str) -> dict:
    """Flat name -> value view of an Octopus system CSV (integers and strings;
    'Extends' chains are not followed, the presets are self-contained)."""
    path = _octopus_root() / "configuration" / "SystemConfigurations" / f"{config_name}.csv"
    values = {}
    for line in path.read_text().splitlines():
        head, _, rest = line.partition(",")
        if "(" not in head or head.lstrip().startswith("#"):
            continue
        name, _, typ = head.partition("(")
        typ = typ.rstrip(")").strip()
        first = rest.split(",")[0].strip()
        if typ == "i" and first:
            values[name.strip()] = int(first)
        elif typ == "s":
            values[name.strip()] = first
    return values


def _preset_int(values: dict, key: str, default: int) -> int:
    if key in values:
        return values[key]
    print(f"gem5_base_cache_hierarchy: {key} not in preset, using {default}")
    return default


def _load_csv_cache_config(config_name: str) -> tuple[_CacheConfig, _CacheConfig]:
    v = _read_preset(config_name)
    l1 = _CacheConfig(
        size_bytes=_preset_int(v, "cache_controller[*].m_data_handler.cache_size", 16384),
        assoc=_preset_int(v, "cache_controller[*].m_data_handler.m_ways_count", 1),
        latency=_preset_int(v, "cache_controller[*].m_data_handler.m_data_access_latency", 0),
        mshrs=_preset_int(v, "cache_controller[*].num_mshr", 16),
    )
    l2 = _CacheConfig(
        size_bytes=_preset_int(v, "llc_controller.m_data_handler.cache_size", 32768),
        assoc=_preset_int(v, "llc_controller.m_data_handler.m_ways_count", 2),
        latency=_preset_int(v, "llc_controller.m_data_handler.m_data_access_latency", 10),
        mshrs=_preset_int(v, "llc_controller.num_mshr", 64),
    )
    return l1, l2


class Gem5BaseCacheHierarchy(AbstractClassicCacheHierarchy):
    """
    A cache hierarchy matching the OctopusCacheHierarchy interface but using
    gem5 base caches with sizes, associativity, and latencies from the Octopus
    system CSV preset (the same one the Octopus hierarchy is built from).
    """

    def _get_default_membus(self) -> SystemXBar:
        membus = SystemXBar(width=64)
        membus.badaddr_responder = BadAddr()
        membus.default = membus.badaddr_responder.pio
        return membus

    def __init__(self, config_name: str, output_path: str, atp_files=None) -> None:
        AbstractClassicCacheHierarchy.__init__(self=self)

        self._config_name = config_name
        self._output_path = output_path
        self._use_atp = atp_files is not None
        self.membus = self._get_default_membus()

        self._l1_cfg, self._l2_cfg = _load_csv_cache_config(config_name)

    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        self.l1i_caches = [
            L1ICache(
                size=self._l1_cfg.size_str(),
                assoc=self._l1_cfg.assoc,
                tag_latency=L1_TAG_LATENCY,
                data_latency=L1_DATA_LATENCY,
                response_latency=L1_RESPONSE_LATENCY,
                mshrs=self._l1_cfg.mshrs,
            )
            for _ in range(board.get_processor().get_num_cores())
        ]

        self.l1d_caches = [
            L1DCache(
                size=self._l1_cfg.size_str(),
                assoc=self._l1_cfg.assoc,
                tag_latency=L1_TAG_LATENCY,
                data_latency=L1_DATA_LATENCY,
                response_latency=L1_RESPONSE_LATENCY,
                mshrs=self._l1_cfg.mshrs,
            )
            for _ in range(board.get_processor().get_num_cores())
        ]

        self.l2bus = L2XBar()
        self.l2cache = L2Cache(
            size=self._l2_cfg.size_str(),
            assoc=self._l2_cfg.assoc,
            tag_latency=self._l2_cfg.latency,
            data_latency=self._l2_cfg.latency,
            response_latency=self._l2_cfg.latency,
            mshrs=self._l2_cfg.mshrs,
        )

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            cpu.connect_icache(self.l1i_caches[i].cpu_side)
            cpu.connect_dcache(self.l1d_caches[i].cpu_side)

            self.l1i_caches[i].mem_side = self.l2bus.cpu_side_ports
            self.l1d_caches[i].mem_side = self.l2bus.cpu_side_ports

            self._connect_table_walker(cpu)

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        self.l2bus.mem_side_ports = self.l2cache.cpu_side
        self.membus.cpu_side_ports = self.l2cache.mem_side

    def _connect_table_walker(self, cpu: BaseCPU) -> None:
        walker_ports = cpu.get_mmu().walkerPorts() if cpu.has_mmu() else []
        if len(walker_ports) == 0:
            return

        cpu.connect_walker_ports(
            self.l2bus.cpu_side_ports,
            self.l2bus.cpu_side_ports,
        )

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        """Create a cache for coherent I/O connections"""
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1KiB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()

    def setLogEn(self, flag) -> None:
        """No-op for interface compatibility with OctopusCacheHierarchy."""
        return
