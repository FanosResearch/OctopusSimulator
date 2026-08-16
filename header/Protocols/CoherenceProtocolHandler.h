/*
 * File  :      CoherenceProtocolHandler.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _CoherenceProtocolHandler_H
#define _CoherenceProtocolHandler_H

#include "FSMReader.h"
#include "ProcessingBuffer.h"
#include "Logger.h"
#include "IdGenerator.h"
#include "CacheDataHandler.h"
#include "ControllerAction.h"
#include "DebugPrint.h"

#include <string.h>

namespace ns3
{   
    enum AllocationType
    {
        ON_MISS,
        ON_REFILL
    };

    class CoherenceProtocolHandler
    {
    protected:
        int m_id;
        int m_shared_memory_id;
        bool m_cache2Cache;
        int m_reqWbRatio;
        AllocationType m_alloc_type;

        FSMReader *m_fsm;
        CacheDataHandler *m_data_handler;

        virtual void allocateLine(uint64_t addr, bool* caused_replacement = NULL);

    public:
        CoherenceProtocolHandler(CacheDataHandler *cache, const std::string& fsm_path, 
                                 int id, int sharedMemId, AllocationType alloc_type = AllocationType::ON_MISS);
        virtual ~CoherenceProtocolHandler();

        virtual std::vector<ControllerAction> processRequest(Message& request_msg, DebugPrint* dprint = NULL) = 0;
        virtual EntryState getRequestState(const Message &, EntryState) = 0;
        virtual void initializeCacheStates();
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) {};
    };
}

#endif
