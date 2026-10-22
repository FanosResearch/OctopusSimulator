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
        std::vector<int> m_home_ids; int m_home_shift = 0;   // LLC banks (spec s24); default = {m_shared_memory_id}
        bool m_cache2Cache;
        int m_reqWbRatio;

        FSMReader *m_fsm;
        CacheDataHandler *m_data_handler;

    public:
        void setHomeMap(const std::vector<int> &ids, int shift) { m_home_ids = ids; m_home_shift = shift; }
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

        // Class annotation for the protocol-and-network deadlock check (spec s14.3): the message a
        // named FSM event consumes and the messages a named action emits onto the interconnect.
        // kind -1 = not an interconnect message (the CPU, the memory, the controller itself).
        // Returning false = the name is unknown to this protocol (the check reports it).
        // memory: travels between the home and a memory controller; on the network only with memory_on_network (spec s25.3)
        // tl_channel: a TileLink protocol's channel, 1..5 = A..E (docs/TileLink_Plan.md s5.2); 0 = not TileLink.
        // With memory set it is the LLC's outer (memory-side) link, otherwise the inner L1<->LLC link.
        // count: messages of this shape one action emits; -1 = one per holder of the line (a probe fan-out)
        struct MsgClass { int kind = -1; bool data = false; bool from_home = false; bool memory = false;
                          int tl_channel = 0; int count = 1; };
        virtual bool consumedBy(const std::string &event, MsgClass &out) const { (void)event; (void)out; return false; }
        virtual bool emittedBy(const std::string &action, std::vector<MsgClass> &out) const { (void)action; (void)out; return false; }
        virtual bool hasClassAnnotation() const { return false; }
        // Whether the named event is raised by the controller itself, not consumed from the CPU, the
        // network or the memory: CacheController::checkReplacements' Replacement (Message::Source::SELF).
        // The check lists every cell of such an event that emits onto the network (spec s14.3,
        // docs/TileLink_Plan.md s5.3 I-C: the fill-triggered write-back is the one edge the class
        // order does not order). Every table names that event Replacement; override if not.
        virtual bool selfRaised(const std::string &event) const { return event == "Replacement"; }
        // transmit admission (spec s14.2): the most forward-class messages a home emits for one demand request
        // with `caches` caches -- a forward plus invalidations of the others (MOESI adds an ack count)
        virtual int demandForwards(int caches) const { return caches - 1; }
        // TileLink transmit admission (docs/TileLink_Plan.md s5.2 point 1): the messages the cell this
        // message will take in the line's current state emits, one MsgClass per shape with tl_channel,
        // memory (outer link) and count set. A probe fan-out (count -1 in emittedBy) is resolved here
        // to the line's current holder count, which cannot grow while the message waits (the line is
        // blocked), so every returned count is >= 0. The self-raised Replacement is predicted from
        // its own cell like any other message. An empty `out` = the transition emits nothing (it is
        // popped unconditionally). Returning false = this protocol does not predict: the controller
        // then keeps the three-queue admission of the existing protocols, unchanged (it asks once).
        virtual bool predictEmissions(const Message &msg, GenericCacheLine &line, std::vector<MsgClass> &out)
        { (void)msg; (void)line; (void)out; return false; }
        // Whether an emission allocates a TileLink identifier at its port (s5.2 point 2): every A (a
        // source), a Release/ReleaseData on C (a source; plan s5.1 kinds K_PUTS, K_PUTE, K_PUTM) and a
        // Grant/GrantData on D (a sink; kind K_RESP). ProbeAck[Data], ReleaseAck, AccessAck[Data] and
        // GrantAck carry the id of the message they answer.
        static bool tlAllocatesId(const MsgClass &c)
        {
            if (c.tl_channel == 1) return true;
            if (c.tl_channel == 3) return c.kind == Message::K_PUTS || c.kind == Message::K_PUTE || c.kind == Message::K_PUTM;
            if (c.tl_channel == 4) return c.kind == Message::K_RESP && !c.memory;
            return false;
        }
        FSMReader *fsm() const { return m_fsm; }
    };
}

#endif
