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
        uint64_t m_ready_cycle;    // uint32_t here wrapped -> isReady() stuck ~4.29B cyc

        virtual inline void *getLine(uint64_t set, int way)
        {
            return (void *)&m_cache[set * m_ways_count + way];
        }

        inline uint64_t calculate_tag(uint64_t address)
        {
            return (address >> ((int)log2(m_block_size) + (int)log2(m_sets_count)));
        }

        inline uint64_t calculate_set(uint64_t address)
        {
            return ((address >> (int)log2(m_block_size)) & (m_sets_count - 1));
        }

        inline uint64_t calculate_address(uint64_t tag, uint64_t set)
        {
            return (tag << ((int)log2(m_block_size) + (int)log2(m_sets_count))) |
                   (set << ((int)log2(m_block_size)));
        }

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
    };
}

#endif /* _CacheDataHandler_H */
