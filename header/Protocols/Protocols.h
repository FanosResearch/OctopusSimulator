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
#include "LLCMSIDirectory.h"
#include "LLCMESIDirectory.h"
#include "LLCMOESIDirectory.h"

#include "MSIProtocol.h"
#include "MESIProtocol.h"
#include "MOESIProtocol.h"
#include "PMSIProtocol.h"
#include "PMESIProtocol.h"
#include "PMSIAsteriskProtocol.h"
#include "PMESIAsteriskProtocol.h"
// #include "Pendulum.h"
#include "MSIDirectory.h"
#include "MESIDirectory.h"
#include "MOESIDirectory.h"

#include <string>

namespace octopus
{
    class Protocols
    {
    public:
        enum ProtocolType
        {
            SNOOP_PMSI,
            SNOOP_PMESI,
            SNOOP_PMSI_ASTERISK,
            SNOOP_PMESI_ASTERISK,
            SNOOP_MSI,
            SNOOP_MESI,
            SNOOP_MOESI,
            SNOOP_LLC_MSI,
            SNOOP_LLC_MESI,
            SNOOP_LLC_MOESI,
            SNOOP_LLC_PMSI,
            SNOOP_LLC_PMESI,
            SNOOP_LLC_PMSI_ASTERISK,
            SNOOP_LLC_PMESI_ASTERISK,
            SNOOP_PENDULUM,
            SNOOP_LLC_PENDULUM,
            DIRECTORY_MSI,
            DIRECTORY_LLC_MSI,
            DIRECTORY_MESI,
            DIRECTORY_LLC_MESI,
            DIRECTORY_MOESI,
            DIRECTORY_LLC_MOESI,
        };

        static CoherenceProtocolHandler* getNewProtocol(std::string type, CacheDataHandler *cache, const string &fsm_path, int cache_id, int shared_mem_id)
        {
            if(type == STRINGIFY(SNOOP_MSI))
                return new MSIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_LLC_MSI) || type == STRINGIFY(SNOOP_LLC_PMSI_ASTERISK))
                return new LLCMSIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_MESI))
                return new MESIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_MOESI))
                return new MOESIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_LLC_MESI) || type == STRINGIFY(SNOOP_LLC_MOESI) || type == STRINGIFY(SNOOP_LLC_PMESI_ASTERISK))
                return new LLCMESIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_PMSI))
                return new PMSIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_LLC_PMSI))
                return new LLCPMSIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_PMESI))
                return new PMESIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_LLC_PMESI))
                return new LLCPMESIProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_PMSI_ASTERISK))
                return new PMSIAsteriskProtocol(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(SNOOP_PMESI_ASTERISK))
                return new PMESIAsteriskProtocol(cache, fsm_path, cache_id, shared_mem_id);
            // else if(type == STRINGIFY(SNOOP_PENDULUM))
            //     return new Pendulum(cache, fsm_path, cache_id, shared_mem_id);
            // else if(type == STRINGIFY(SNOOP_LLC_PENDULUM))
            //     return new LLCPendulum(cache, fsm_path, cache_id);
            else if(type == STRINGIFY(DIRECTORY_MSI))
                return new MSIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(DIRECTORY_LLC_MSI))
                return new LLCMSIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(DIRECTORY_MESI))
                return new MESIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(DIRECTORY_LLC_MESI))
                return new LLCMESIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(DIRECTORY_MOESI))
                return new MOESIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else if(type == STRINGIFY(DIRECTORY_LLC_MOESI))
                return new LLCMOESIDirectory(cache, fsm_path, cache_id, shared_mem_id);
            else
            {
                cout << "Error: Not recognized protocol type" << endl;
                return NULL;
            }
        }
    };
}

#endif