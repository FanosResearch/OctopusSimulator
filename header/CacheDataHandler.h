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

#include <math.h>

namespace octopus
{
    class CacheDataHandler : public Configurable
    {
    protected:
        GenericCacheLine *m_cache;
        uint32_t m_block_size;
        uint32_t m_ways_count;
        // way partitioning (way_partition(s), e.g. "0:0;1-3:1"): the ways a core may fill;
        // cores not listed share the ways nobody claimed. Applied when the controller names the
        // requester of the fill (setRequester); -1 = unrestricted.
        std::map<int, uint32_t> m_way_mask;
        uint32_t m_shared_mask = 0, m_all_mask = 0;
        int m_requester = -1;
        uint32_t m_sets_count;

        // ReplcPolicy m_replacement_policy;
        ReplacementPolicy *m_replacement_policy;
        DebugPrint *dprint;

        uint64_t m_cycle;          // 64-bit: giant traces exceed 2^32 cycles; a
        uint32_t m_data_access_latency;
        uint64_t m_ready_cycle;
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

        // Set, tag and the inverse. A cache that is one of N interleaved banks (an LLC bank:
        // line -> bank (line >> shift) % N) indexes its sets with the bank-select digit removed,
        // so every bank uses all of its sets; a cache that is not banked (N = 1) uses the plain
        // modulo split. Same semantics as AddressMap/Interleave on llc-banking.
        int m_block_bits = 0, m_set_bits = 0;
        int m_bank_n = 1, m_bank_shift = 0, m_bank_idx = -1;   // m_bank_idx < 0 = not a bank

        inline uint64_t packed_line(uint64_t address) const
        {
            uint64_t line = address >> m_block_bits;
            if (m_bank_idx < 0) return line;
            if ((int)((line >> m_bank_shift) % (uint64_t)m_bank_n) != m_bank_idx) foreignLine(address);
            uint64_t low = line & ((1ull << m_bank_shift) - 1);
            return (((line >> m_bank_shift) / (uint64_t)m_bank_n) << m_bank_shift) | low;
        }

        inline uint64_t calculate_tag(uint64_t address)
        {
            return packed_line(address) >> m_set_bits;
        }

        inline uint64_t calculate_set(uint64_t address)
        {
            return packed_line(address) & (m_sets_count - 1);
        }

        inline uint64_t calculate_address(uint64_t tag, uint64_t set)
        {
            uint64_t p = (tag << m_set_bits) | set;
            if (m_bank_idx >= 0)
            {
                uint64_t low = p & ((1ull << m_bank_shift) - 1);
                p = (((p >> m_bank_shift) * (uint64_t)m_bank_n + (uint64_t)m_bank_idx) << m_bank_shift) | low;
            }
            return p << m_block_bits;
        }

        [[noreturn]] void foreignLine(uint64_t address) const;   // a bank asked about another bank's line

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

        // This cache is bank `self_idx` of `n` banks that interleave lines every 2^shift lines.
        // n <= 1 or self_idx < 0 = not banked.
        void setBanks(int n, int shift, int self_idx)
        {
            bool banked = n > 1 && self_idx >= 0 && self_idx < n;
            m_bank_n = banked ? n : 1;
            m_bank_shift = banked ? shift : 0;
            m_bank_idx = banked ? self_idx : -1;
        }
        bool banked() const { return m_bank_idx >= 0; }
        int bankIndex() const { return m_bank_idx; }
        uint32_t getSetsCount() const { return m_sets_count; }

        virtual bool writeCacheLine_bypassLatency(uint64_t address, GenericCacheLine *line, bool soft_write = false);
        virtual bool writeCacheLine(uint64_t address, GenericCacheLine *line);
        virtual bool updateLineBits(uint64_t address, GenericCacheLine *line);
        virtual bool updateLineData(uint64_t address, const uint8_t *data);
        virtual bool modifyData(uint64_t address, const uint8_t *data, uint16_t size, bool soft_write = false);

        virtual bool readCacheLine(uint64_t address, GenericCacheLine *out_line = NULL, bool soft_read = false);
        virtual bool readLineBits(uint64_t address, GenericCacheLine *out_line = NULL);

        int findEmptyWay(uint64_t address);
        uint32_t allowedWays() const
        {
            if (m_requester < 0 || m_way_mask.empty()) return m_all_mask;
            auto it = m_way_mask.find(m_requester);
            return it != m_way_mask.end() ? it->second : m_shared_mask;
        }
        void setRequester(int core) { m_requester = core; }
        bool partitioned() const { return !m_way_mask.empty(); }

        uint64_t getEvictionCandidate(uint64_t address, GenericCacheLine *line);
        
        void updateCycle(uint64_t cycle);
        uint64_t getCycle() { return m_cycle; }
        virtual bool isReady();
        virtual bool isReady(uint64_t address);

        // Where a block's bytes live. Anything other than the array (the
        // MSHR and write-back buffer of a derived handler) is a register
        // file read and written in place: an access to it never enters the
        // data array pipeline. The base handler has only the array.
        enum class LineLocation { NONE, ARRAY, MSHR, PWB };
        virtual LineLocation lineLocation(uint64_t address);
    };
}

#endif /* _CacheDataHandler_H */
