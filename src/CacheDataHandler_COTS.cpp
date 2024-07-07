/*
 * File  :      CacheDataHandler_COTS.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 20, 2022
 */
#include "../header/CacheDataHandler_COTS.h"

namespace octopus
{
    CacheDataHandler_COTS::CacheDataHandler_COTS(ParametersMap map, string pname, string config_path, string name)
        : CacheDataHandler(map, pname, config_path, name)
    {
        line_added2PWB = false;
        address_of_recently_added2PWB = 0;
    }

    CacheDataHandler_COTS::~CacheDataHandler_COTS()
    {
    }

    inline void *CacheDataHandler_COTS::getLine(uint64_t set, int way)
    {
        if (way >= 0)
            return CacheDataHandler::getLine(set, way);
        else if (checkMSHR(set))
            return (void *)&m_miss_status_holding_regs[set];
        else if (checkPWB(set))
            return (void *)&m_pending_write_back_regs[set];

        return NULL;
    }

    bool CacheDataHandler_COTS::findline(uint64_t address, uint64_t *set, int *way)
    {
        if (CacheDataHandler::findline(address, set, way))
            return true;

        *way = -1;

        if (checkMSHR(mask_offset(address)))
        {
            *set = mask_offset(address);
            return true;
        }
        else if (checkPWB(mask_offset(address)))
        {
            *set = mask_offset(address);
            return true;
        }

        return false;
    }

    void CacheDataHandler_COTS::writeLine2MSHR(uint64_t address, GenericCacheLine *line)
    {
        if (line->valid == false)
            return;
        m_miss_status_holding_regs[mask_offset(address)] = *line;
        m_miss_status_holding_regs[mask_offset(address)].m_block_size = this->m_block_size;
    }

    void CacheDataHandler_COTS::moveLine2WB(uint64_t set, int way)
    {
        GenericCacheLine *line = (GenericCacheLine *)getLine(set, way);
        m_pending_write_back_regs[calculate_address(line->tag, set)] = *line;
        line->valid = false;

        address_of_recently_added2PWB = calculate_address(line->tag, set);
        line_added2PWB = true;
    }

    bool CacheDataHandler_COTS::updateLineData(uint64_t address, const uint8_t *data)
    {
        uint64_t set;
        int way;

        if (CacheDataHandler::findline(address, &set, &way))
            return CacheDataHandler::updateLineData(address, data);

        if (checkMSHR(mask_offset(address)))
        {
            m_miss_status_holding_regs[mask_offset(address)].copyData(data);
            if (findEmptyWay(address) == -1) //ToDo: add clean flag
                moveLine2WB(set, chooseEvictionWay(set));

            if (writeCacheLine(address, &m_miss_status_holding_regs[mask_offset(address)]))
            {
                m_miss_status_holding_regs.erase(mask_offset(address));
                return true;
            }
            return false;
        }
        else if (checkPWB(mask_offset(address)))
        {
            m_pending_write_back_regs[mask_offset(address)].copyData(data);
            return true;
        }

        return false;
    }

    bool CacheDataHandler_COTS::updateLineBits(uint64_t address, GenericCacheLine *line)
    {
        bool return_value = CacheDataHandler::updateLineBits(address, line);

        uint64_t set;
        int way;
        if (findline(address, &set, &way))
        {
            if (way < 0 && line->valid == false)
            {
                if (checkMSHR(set))
                    m_miss_status_holding_regs.erase(set);
                else if (checkPWB(set))
                    m_pending_write_back_regs.erase(set);
            }
        }

        return return_value;
    }

    int CacheDataHandler_COTS::chooseEvictionWay(uint64_t set)
    {
        int way;
        
        m_replacement_policy->getReplacementCandidate(set, &way);
        return way;
    }

    bool CacheDataHandler_COTS::addressOfLinePendingWB(bool clear_flag, uint64_t *address)
    {
        if (line_added2PWB)
        {
            *address = address_of_recently_added2PWB;
            line_added2PWB = clear_flag ? false : line_added2PWB;
            return true;
        }

        return false;
    }

    bool CacheDataHandler_COTS::isReady(uint64_t address)
    {
        if (checkMSHR(mask_offset(address)) || checkPWB(mask_offset(address)))
            return true;
        return CacheDataHandler::isReady(address);
    }
}