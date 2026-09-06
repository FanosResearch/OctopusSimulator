/*
 * File  :      CacheDataHandler_COTS.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 20, 2022
 */
#include "../header/CacheDataHandler_COTS.h"
#include "../header/Protocols/TraceTransition.h"

namespace octopus
{
    CacheDataHandler_COTS::CacheDataHandler_COTS(ParametersMap map, string pname, string config_path, string name)
        : CacheDataHandler(map, pname, config_path, name)
    {
        // Optional write-back-buffer depth. Read from config when present,
        // otherwise a finite realistic default. -1 = unbounded (legacy).
        m_pwb_size = DEFAULT_PWB_SIZE;
        if (parameters.find(STRINGIFY(pwb_size)) != parameters.end())
            m_pwb_size = std::get<int>(parameters.at(STRINGIFY(pwb_size)).value);
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
        if (octopus::traceHit(address, m_block_size))
            std::cout << "[MSHR-W2M a=0x" << std::hex << address << std::dec
                      << " existed=" << checkMSHR(mask_offset(address))
                      << " existHadData=" << (checkMSHR(mask_offset(address))?(m_miss_status_holding_regs[mask_offset(address)].m_data!=NULL):0)
                      << " newSt=" << line->state << " newHasData=" << (line->m_data!=NULL) << "]" << std::endl;
        m_miss_status_holding_regs[mask_offset(address)] = *line;
        m_miss_status_holding_regs[mask_offset(address)].m_block_size = this->m_block_size;
    }

    void CacheDataHandler_COTS::moveLine2WB(uint64_t set, int way)
    {
        GenericCacheLine *line = (GenericCacheLine *)getLine(set, way);
        uint64_t wb_address = calculate_address(line->tag, set);
        if (octopus::traceHit(wb_address, m_block_size))
            std::cout << "[WBLC cyc=" << m_cycle << " MOVE2WB a=0x" << std::hex << wb_address << std::dec
                      << " fromWay=" << way << " st=" << line->state << "]" << std::endl;
        m_pending_write_back_regs[wb_address] = *line;
        line->valid = false;

        // Queue this eviction so its write-back is always issued (see header).
        m_pwb_pending_issue.push_back(wb_address);
    }

    bool CacheDataHandler_COTS::fillMSHRData(uint64_t address, const uint8_t *data)
    {
        // Copy the just-arrived block into the existing MSHR entry (fill buffer). No
        // eviction, no promotion, no state change -- just make the data available so an
        // in-flight forward can read it before the (latency-gated) bank promotion lands.
        if (data != NULL && checkMSHR(mask_offset(address)))
        {
            m_miss_status_holding_regs[mask_offset(address)].copyData(data);
            if (octopus::traceHit(address, m_block_size))
                std::cout << "[MSHR-FILL a=0x" << std::hex << address << std::dec
                          << " st=" << m_miss_status_holding_regs[mask_offset(address)].state
                          << " dataNowSet=" << (m_miss_status_holding_regs[mask_offset(address)].m_data!=NULL) << "]" << std::endl;
            return true;
        }
        return false;
    }

    bool CacheDataHandler_COTS::hasMSHRData(uint64_t address)
    {
        uint64_t key = mask_offset(address);
        return checkMSHR(key) && m_miss_status_holding_regs[key].m_data != NULL;
    }

    uint8_t *CacheDataHandler_COTS::peekData(uint64_t address)
    {
        uint64_t set; int way;
        if (findline(address, &set, &way))
        {
            GenericCacheLine *cl = (GenericCacheLine *)getLine(set, way);
            if (cl != NULL && cl->valid && cl->m_data != NULL)
                return cl->m_data;
        }
        return NULL;
    }

    void CacheDataHandler_COTS::promoteFromMSHR(uint64_t address)
    {
        uint64_t key = mask_offset(address);
        if (!checkMSHR(key) || m_miss_status_holding_regs[key].m_data == NULL)
            return; // not buffered, or data has not arrived yet

        uint64_t set; int way;
        CacheDataHandler::findline(address, &set, &way); // sets `set` = cache set index
        if (findEmptyWay(address) == -1)
            moveLine2WB(set, chooseEvictionWay(set));

        if (writeCacheLine(address, &m_miss_status_holding_regs[key]))
            m_miss_status_holding_regs.erase(key);
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
                if (octopus::traceHit(address, m_block_size))
                    std::cout << "[MSHR-ERASE-PROMO a=0x" << std::hex << address << std::dec << "]" << std::endl;
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
                if (checkMSHR(set)) {
                    if (octopus::traceHit(address, m_block_size))
                        std::cout << "[MSHR-ERASE-INVAL a=0x" << std::hex << address << std::dec << "]" << std::endl;
                    m_miss_status_holding_regs.erase(set);
                }
                else if (checkPWB(set))
                {
                    if (octopus::traceHit(address, m_block_size))
                        std::cout << "[WBLC cyc=" << m_cycle << " PWB-ERASE a=0x" << std::hex << address << std::dec
                                  << " (line -> Invalid while in WB)]" << std::endl;
                    m_pending_write_back_regs.erase(set);
                }
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
        // Peek (clear_flag == false) or pop (clear_flag == true) the oldest
        // not-yet-issued write-back address. Backed by a queue so no eviction
        // is dropped when several happen before checkReplacements runs.
        if (m_pwb_pending_issue.empty())
            return false;

        *address = m_pwb_pending_issue.front();
        if (clear_flag && octopus::traceHit(*address, m_block_size))
            std::cout << "[WBLC cyc=" << m_cycle << " PENDING-ISSUE-POP a=0x" << std::hex << *address << std::dec
                      << " inPWB=" << (checkPWB(mask_offset(*address)) ? 1 : 0) << "]" << std::endl;
        if (clear_flag)
            m_pwb_pending_issue.erase(m_pwb_pending_issue.begin());
        return true;
    }

    bool CacheDataHandler_COTS::isReady(uint64_t address)
    {
        if (checkMSHR(mask_offset(address)) || checkPWB(mask_offset(address)))
            return true;
        return CacheDataHandler::isReady(address);
    }
}