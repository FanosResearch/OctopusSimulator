/*
 * File  :      CacheDataArray.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 27, 2022
 */
#include "../header/CacheDataHandler.h"

namespace ns3
{
    CacheDataHandler::CacheDataHandler(CacheXml &cacheXml, ReplacementPolicy* policy)
    {
        int lines_count = cacheXml.GetCacheSize() / cacheXml.GetBlockSize();

        m_cache = new GenericCacheLine[lines_count];
        m_block_size = cacheXml.GetBlockSize();
        m_ways_count = cacheXml.GetNWays();
        m_sets_count = lines_count / cacheXml.GetNWays();

        m_replacement_policy = policy;

        m_data_access_latency = cacheXml.GetDataAccessLatency();

        m_cycle = 0;
        m_ready_cycle = 0;
    }

    CacheDataHandler::~CacheDataHandler()
    {
        delete[] m_cache;
    }

    void CacheDataHandler::initializeCacheStates(int initialState)
    {
        for (uint32_t set = 0; set < m_sets_count; set++)
        {
            for(uint32_t way = 0; way < m_ways_count; way++)
            {
                ((GenericCacheLine *)getLine(set, way))->state = initialState;
                ((GenericCacheLine *)getLine(set, way))->valid = false;
                ((GenericCacheLine *)getLine(set, way))->m_block_size = this->m_block_size;
            }
        }
    }

    void CacheDataHandler::initializeCacheLine(GenericCacheLine *line)
    {
        *line = GenericCacheLine();
        line->m_block_size = this->m_block_size;
        line->m_data = new uint8_t[line->m_block_size];
        
        memset(line->m_data, 0, line->m_block_size);
    }

    bool CacheDataHandler::findline(uint64_t address, uint64_t *set, int *way)
    {
        *set = calculate_set(address);

        for (uint32_t index = 0; index < m_ways_count; index++)
        {
            GenericCacheLine *cache_line = (GenericCacheLine *)getLine(*set, index);

            if (cache_line->valid == true && cache_line->tag == (int64_t)calculate_tag(address))
            {
                *way = index;
                return true;
            }
        }
        return false;
    }

    bool CacheDataHandler::writeCacheLine_bypassLatency(uint64_t address, GenericCacheLine *line, bool soft_write)
    {
        uint64_t set = calculate_set(address);
        int way = findEmptyWay(address);
        if (way == -1)
            return false;

        *((GenericCacheLine *)getLine(set, way)) = *line;
        ((GenericCacheLine *)getLine(set, way))->tag = calculate_tag(address);
        
        if(!soft_write)
            m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss

        return true;
    }

    bool CacheDataHandler::writeCacheLine(uint64_t address, GenericCacheLine *line)
    {
        if (!isReady(address))
            return false;

        if(line->m_data != NULL)
            m_ready_cycle = m_cycle + m_data_access_latency;
        
        return writeCacheLine_bypassLatency(address, line);
    }

    bool CacheDataHandler::updateLineBits(uint64_t address, GenericCacheLine *line)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way))
        {
            ((GenericCacheLine *)getLine(set, way))->copyBits(*line);
            ((GenericCacheLine *)getLine(set, way))->m_block_size = this->m_block_size;
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::updateLineData(uint64_t address, const uint8_t *data)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && isReady(address))
        {
            ((GenericCacheLine *)getLine(set, way))->copyData(data);
            m_ready_cycle = m_cycle + m_data_access_latency;
            m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::modifyData(uint64_t address, const uint8_t *data, uint16_t size, bool soft_write)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && (isReady(address) || soft_write))
        {
            uint16_t offset = address & (m_block_size - 1);
            ((GenericCacheLine *)getLine(set, way))->modifyData(data, offset, size);

            m_ready_cycle = m_cycle + m_data_access_latency;
            if(!soft_write)
                m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::readCacheLine(uint64_t address, GenericCacheLine *out_line, bool soft_read)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && isReady(address))
        {
            if (out_line != NULL)
            {    
                *out_line = *((GenericCacheLine *)getLine(set, way));
                m_ready_cycle = m_cycle + m_data_access_latency;
            }
            if(!soft_read)
                m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::readLineBits(uint64_t address, GenericCacheLine *out_line)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way))
        {
            if (out_line != NULL)
                out_line->copyBits(*((GenericCacheLine *)getLine(set, way)));
            return true;
        }
        else
            return false;
    }

    int CacheDataHandler::findEmptyWay(uint64_t address)
    {
        uint64_t set = calculate_set(address);

        for (uint32_t way = 0; way < m_ways_count; way++)
        {
            GenericCacheLine *cache_line = (GenericCacheLine *)getLine(set, way);

            if (cache_line->valid == false)
            {
                return way;
            }
        }
        return -1;
    }

    uint64_t CacheDataHandler::getEvictionCandidate(uint64_t address, GenericCacheLine *line)
    {
        // if (m_replacement_policy == ReplcPolicy::RANDOM)
        // {
        //     uint64_t set = calculate_set(address);
        //     uint64_t way = rand() % m_ways_count;
            
        //     line->copyBits(*((GenericCacheLine *)getLine(set, way)));
        //     return calculate_address(((GenericCacheLine *)getLine(set, way))->tag, set);
        // }
        return 0;
    }

    void CacheDataHandler::updateCycle(uint64_t cycle)
    {
        m_cycle = cycle;
    }

    bool CacheDataHandler::isReady()
    {
        if(m_ready_cycle <= m_cycle)
            return true;
        
        return false;
    }

    bool CacheDataHandler::isReady(uint64_t address)
    {
        return isReady();
    }
}