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
#include "FRFCFS_Buffer.h"
#include "Logger.h"
#include "IdGenerator.h"
#include "CacheDataHandler.h"
#include "ControllerAction.h"
#include "DebugPrint.h"

#include <string.h>

namespace octopus
{   
    class CoherenceProtocolHandler
    {
    protected:
        int m_id;
        int m_shared_memory_id;
        bool m_cache2Cache;
        int m_reqWbRatio;

        FSMReader *m_fsm;
        CacheDataHandler *m_data_handler;

    public:
        CoherenceProtocolHandler(CacheDataHandler *cache, const std::string& fsm_path, int id, int sharedMemId);
        virtual ~CoherenceProtocolHandler();

        virtual std::vector<ControllerAction> processRequest(Message& request_msg, DebugPrint* dprint = NULL) = 0;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) = 0;
        virtual void initializeCacheStates();
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) {};
    };
}

#endif
