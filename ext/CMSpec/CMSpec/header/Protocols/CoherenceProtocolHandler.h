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

#include <string.h>

namespace ns3
{   
    struct ControllerAction
    {
        enum Type
        {
            REMOVE_PENDING = 0,
            HIT_Action,
            ADD_PENDING,
            SEND_BUS_MSG,
            WRITE_BACK,
            UPDATE_CACHE_LINE,
            WRITE_CACHE_LINE_DATA,
            MODIFY_DATA,
            SAVE_REQ_FOR_WRITE_BACK,
            NO_ACTION,
            REMOVE_SAVED_REQ,
            TIMER_ACTION,
            ADD_SHARER,
            REMOVE_SHARER,
            SEND_INV_MSG,
            STALL,
        } type;
    
        void* data;
    };

    enum CohProtType
    {
        SNOOP_PMSI = 0x000,
        SNOOP_PMESI = 0x001,
        SNOOP_PMSI_ASTERISK = 0x002,
        SNOOP_PMESI_ASTERISK = 0x003,
        SNOOP_MSI = 0x100,
        SNOOP_MESI = 0x200,
        SNOOP_MOESI = 0x300,
        SNOOP_LLC_MSI = 0x400,
        SNOOP_LLC_MESI = 0x500,
        SNOOP_LLC_MOESI = 0x600,
        SNOOP_LLC_PMSI = 0x700,
        SNOOP_LLC_PMESI = 0x800,
        SNOOP_LLC_PMSI_ASTERISK = 0x900,
        SNOOP_LLC_PMESI_ASTERISK = 0xA00,
        SNOOP_PENDULUM = 0xB00,
        SNOOP_LLC_PENDULUM = 0xC00
    };

    class CoherenceProtocolHandler
    {
    protected:
        int m_core_id;
        int m_shared_memory_id;
        bool m_cache2Cache;
        int m_reqWbRatio;

        FSMReader *m_fsm;
        CacheDataHandler *m_data_handler;

    public:
        CoherenceProtocolHandler(CacheDataHandler *cache, const std::string& fsm_path, int coreId, int sharedMemId);
        virtual ~CoherenceProtocolHandler();

        virtual const std::vector<ControllerAction>& processRequest(Message& request_msg) = 0;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) = 0;
        virtual void initializeCacheStates();
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) {};
    };
}

#endif
