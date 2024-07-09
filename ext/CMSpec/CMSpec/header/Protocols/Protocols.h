/*
 * File  :      Protocols.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 10, 2021
 */

#ifndef _PROTOCOLS_H
#define _PROTOCOLS_H

#include "CoherenceProtocolHandler.h"

#include "LLCMESIProtocol.h"
#include "LLCMSIProtocol.h"
#include "LLCPMSIProtocol.h"
#include "LLCPMESIProtocol.h"
// #include "LLCPendulum.h"

#include "MSIProtocol.h"
#include "MESIProtocol.h"
#include "MOESIProtocol.h"
#include "PMSIProtocol.h"
#include "PMESIProtocol.h"
#include "PMSIAsteriskProtocol.h"
#include "PMESIAsteriskProtocol.h"
// #include "Pendulum.h"

namespace ns3
{
    class Protocols
    {
    public:

        static CoherenceProtocolHandler* getNewProtocol(CohProtType pType, CacheDataHandler *cache, const string &fsm_path, int core_id, int shared_mem_id)
        {
            switch (pType)
            {
            case CohProtType::SNOOP_MSI:
                return new MSIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_LLC_MSI:
            case CohProtType::SNOOP_LLC_PMSI_ASTERISK:
                return new LLCMSIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_MESI:
                return new MESIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_MOESI:
                return new MOESIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_LLC_MESI:
            case CohProtType::SNOOP_LLC_MOESI:
            case CohProtType::SNOOP_LLC_PMESI_ASTERISK:
                return new LLCMESIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_PMSI:
                return new PMSIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_LLC_PMSI:
                return new LLCPMSIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_PMESI:
                return new PMESIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_LLC_PMESI:
                return new LLCPMESIProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_PMSI_ASTERISK:
                return new PMSIAsteriskProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            case CohProtType::SNOOP_PMESI_ASTERISK:
                return new PMESIAsteriskProtocol(cache, fsm_path, core_id, shared_mem_id);
                
            // case CohProtType::SNOOP_PENDULUM:
            //     return new Pendulum(cache, fsm_path, core_id, shared_mem_id);
                
            // case CohProtType::SNOOP_LLC_PENDULUM:
            //     return new LLCPendulum(cache, fsm_path, core_id);
            
            default:
                return NULL;
            }
            return NULL;
        }
    };
}

#endif