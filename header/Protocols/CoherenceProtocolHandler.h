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
        // home map: the LLC bank that serves each line (docs/LLC_Banking_and_Partitioning_Design.md
        // s4.2); empty = the single m_shared_memory_id
        Interleave m_home;
        bool m_cache2Cache;
        int m_reqWbRatio;

        FSMReader *m_fsm;
        CacheDataHandler *m_data_handler;

    public:
        void setHomeMap(const Interleave &home) { m_home = home; }
        int  homeOf(uint64_t addr) const;
        bool isHome(int id) const;
        CoherenceProtocolHandler(CacheDataHandler *cache, const std::string& fsm_path, int id, int sharedMemId);
        virtual ~CoherenceProtocolHandler();

        virtual std::vector<ControllerAction> processRequest(Message& request_msg, DebugPrint* dprint = NULL) = 0;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) = 0;
        virtual void initializeCacheStates();
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) {};
        // Whether a line in this state still gives the core a readable copy.
        // Drives the invalidation an external core model receives when a
        // line leaves such a state. Default: no state is readable, which
        // makes the L1 report nothing.
        virtual bool isReadableState(int state) { return false; }
        // Whether processing this message in the line's current state reads
        // or writes the data array (a hit, a data response to a snoop, a
        // fill). A pipelined array defers such a message as a whole, so the
        // state changes only when the bytes have moved. Default: unknown.
        virtual bool needsDataArray(const Message &msg) { return false; }
    };
}

#endif
