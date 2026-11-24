/*
 * File  :      CacheDataHandler.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 27, 2022
 */

#ifndef _CacheDataHandler_H
#define _CacheDataHandler_H

#include "Configurable.h"
#include "DebugPrint.h"
#include "CacheXml.h"
#include "GenericCacheLine.h"
#include "Policy.h"
#include "ReplacementPolicy.h"
#include "AddressMap.h"
#include "Partitioning.h"

#include <math.h>

namespace octopus
{
    class CacheDataHandler : public Configurable
    {
    protected:
        GenericCacheLine *m_cache;
        uint32_t m_block_size;
        uint32_t m_ways_count;
        // Partitioning (design doc s6): partition[k].scheme/ownership/owners, or the
        // way_partition(s) alias. The fill paths take the requesting core (-1 = unknown:
        // unrestricted).
        PartitionSet m_partitions;
        uint32_t m_all_mask = 0;
        // Built once the system is complete (finalizePartitions), since colour and bank schemes
        // depend on the bank group the system builder installs after the caches exist; built
        // lazily on first use if a builder does not call it.
        bool m_partitions_built = false;
        int m_mshr_entries = -1;
        PartitionSet::Waiting m_waiting;   // set by the controller's demand gate
        void ensurePartitions() { if (!m_partitions_built) { m_partitions_built = true; buildPartitions(); } }
        void buildPartitions();
        static std::vector<CacheDataHandler *> &handlers();   // every live handler
        uint32_t m_sets_count;
        // set / tag / address of a line (docs/LLC_Banking_and_Partitioning_Design.md s5)
        AddressMap m_map;

        // ReplcPolicy m_replacement_policy;
        ReplacementPolicy *m_replacement_policy;
        DebugPrint *dprint;

        uint64_t m_cycle;          // 64-bit: giant traces exceed 2^32 cycles; a
        uint32_t m_data_access_latency;
        // Occupancy model: the cycle each data-array sub-bank frees (sub_banks(i), default 1 =
        // the whole array; sub_bank_shift(i) sets the granularity, design doc s5.4). An access
        // waits only for its own sub-bank.
        std::vector<uint64_t> m_sub_ready;
        void occupy(uint64_t address) { if (!m_data_array_pipelined) m_sub_ready[m_map.subBank(address)] = m_cycle + m_data_access_latency; }
        // Data-array model. 0 (default): occupancy -- an access returns at once
        // and closes the array for m_data_access_latency cycles, so one access
        // starts per latency window and none pays the latency. 1: pipelined --
        // the controller admits up to m_data_array_ports accesses per cycle and
        // completes each one m_data_access_latency cycles later, so every
        // access pays the latency and the array never closes.
        bool m_data_array_pipelined;
        uint32_t m_data_array_ports;    // uint32_t here wrapped -> isReady() stuck ~4.29B cyc

        virtual inline void *getLine(uint64_t set, int way)
        {
            return (void *)&m_cache[set * m_ways_count + way];
        }

        inline uint64_t calculate_tag(uint64_t address) { return m_map.tag(address); }
        inline uint64_t calculate_set(uint64_t address) { return m_map.set(address); }
        inline uint64_t calculate_address(uint64_t tag, uint64_t set) { return m_map.address(tag, set); }

        virtual bool findline(uint64_t address, uint64_t *set, int *way);

    public:
        CacheDataHandler(ParametersMap map,
                         string pname = "",
                         string config_path = string(CONFIGURATION_PATH),
                         string name = STRINGIFY(CacheDataHandler));
        virtual ~CacheDataHandler();

        virtual void initializeCacheStates(int initialState);
        virtual void initializeCacheLine(GenericCacheLine *line);

        virtual uint32_t getBlockSize();
        virtual uint32_t getDataAccessLatency();
        bool isPipelined() const { return m_data_array_pipelined; }
        uint32_t getDataArrayPorts() const { return m_data_array_ports; }

        // `core`: the requester a fill is made for; it picks the ways the line may land in
        virtual bool writeCacheLine_bypassLatency(uint64_t address, GenericCacheLine *line, bool soft_write = false, int core = -1);
        virtual bool writeCacheLine(uint64_t address, GenericCacheLine *line, int core = -1);
        virtual bool updateLineBits(uint64_t address, GenericCacheLine *line);
        virtual bool updateLineData(uint64_t address, const uint8_t *data, int core);
        virtual bool modifyData(uint64_t address, const uint8_t *data, uint16_t size, bool soft_write = false);

        virtual bool readCacheLine(uint64_t address, GenericCacheLine *out_line = NULL, bool soft_read = false);
        virtual bool readLineBits(uint64_t address, GenericCacheLine *out_line = NULL);

        // an empty way of the set that `core` may fill (-1 = any way)
        int findEmptyWay(uint64_t address, int core = -1);
        uint32_t allowedWays(int core, uint64_t set) const { return m_partitions.allowedWays(core, set, m_all_mask); }
        const PartitionSet &partitions() const { return m_partitions; }
        // Entry partitions (MSHR and write-back buffer, s6.10). The controller gives its MSHR
        // count before the partitions are built; an MSHR scheme, or a requester-charged write-back
        // buffer scheme, gates demand requests at the sender (admitsDemand). The controller
        // installs `waiting`: per core, its requests at this cache that may still open a miss and
        // the misses it has open. Entries follow entryOpened / entryFreed, keyed by line address.
        void setMshrEntries(int n) { m_mshr_entries = n; }
        virtual int pwbEntries() const { return -1; }
        bool gatesDemand() { ensurePartitions(); return m_partitions.gatesDemand(); }
        void setWaiting(PartitionSet::Waiting w) { m_waiting = std::move(w); }
        bool admitsDemand(int core) const
        {
            std::map<int, int> queued, outstanding;
            if (m_waiting) m_waiting(queued, outstanding);
            return m_partitions.admitsDemand(core, queued, outstanding);
        }
        void entryOpened(PartitionScheme::Entry e, int core, uint64_t key, int placed_by = -1)
        { ensurePartitions(); if (m_partitions.hasEntrySchemes()) m_partitions.allocate(e, core, key, placed_by, &m_waiting); }
        // a fill by `evictor` evicted line `line` (EvictedByOther column)
        void lineEvicted(int placed_by, int evictor) { if (!m_partitions.empty()) m_partitions.onEvicted(placed_by, evictor); }
        void entryFreed(PartitionScheme::Entry e, uint64_t key)
        { if (m_partitions.hasEntrySchemes()) m_partitions.release(e, key); }
        // End of system construction: build every cache's partitions, then configure the page
        // placer (page_placer(i) of the system) with their placing schemes.
        static void finalizePartitions(bool page_placer, int num_cores = 0);
        // The address map (design doc s5): geometry, bank group, sub-banks and home map. The
        // controller installs the home map and the bank group; the protocol reads it.
        AddressMap &addressMap() { return m_map; }
        const AddressMap &addressMap() const { return m_map; }

        uint64_t getEvictionCandidate(uint64_t address, GenericCacheLine *line);
        
        void updateCycle(uint64_t cycle);
        uint64_t getCycle() { return m_cycle; }
        virtual bool isReady();                 // some sub-bank is free (the array, with one)
        virtual bool isReady(uint64_t address);
        // The sub-bank holding `address` is free (no MSHR / write-back-buffer exemption).
        bool portFree(uint64_t address) const { return m_data_array_pipelined || m_sub_ready[m_map.subBank(address)] <= m_cycle; }
        int subBanks() const { return m_map.subBanks(); }
        int subBank(uint64_t address) const { return m_map.subBank(address); }

        // Where a block's bytes live. Anything other than the array (the
        // MSHR and write-back buffer of a derived handler) is a register
        // file read and written in place: an access to it never enters the
        // data array pipeline. The base handler has only the array.
        enum class LineLocation { NONE, ARRAY, MSHR, PWB };
        virtual LineLocation lineLocation(uint64_t address);
    };
}

#endif /* _CacheDataHandler_H */
