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
        // Snooping: whether this protocol's LLC waits for a copy of a deferred supply to a parked
        // GetS (MESI: S_d / MN_d). MOESI's LLC does not: E + GetS -> ClearOwner/S lets the owner
        // supply the reader alone, and a copy arriving afterwards would fault it.
        virtual bool llcExpectsDeferredSupplyCopy() const { return true; }
        // the next-level cache serving a line: read from the data handler's address map
        int  homeOf(uint64_t addr) const { return m_data_handler->addressMap().home(addr); }
        bool isHome(int id) const { return m_data_handler->addressMap().isHome(id); }
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
