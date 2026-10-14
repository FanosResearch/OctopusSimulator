/*
 * File  :      BaseController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 13, 2023
 */

#ifndef _BaseController_H
#define _BaseController_H

#include "CommunicationInterface.h"
#include "CacheDataHandler.h"
#include "CacheXml.h"
#include "ClockManager.h"
#include "Initializable.h"
#include "Configurable.h"
#include "DebugPrint.h"

#include "Protocols.h"
#include "FRFCFS_Buffer.h"
#include "Logger.h"

#include <functional>
#include <ostream>
#include <string>
#include <queue>
#include <vector>
#include <map>

namespace octopus
{
    class ExternalCPU;
    class BaseController : public ClockedObj, public Initializable, public Configurable
    {
    protected:
        int m_id;
        int m_shared_memory_id;
        // LLC banking (docs/NoC_Architecture_Spec.md s24): the homes of this controller's lines,
        // interleaved by line address; one home = today's m_shared_memory_id
        std::vector<int> m_home_ids; int m_home_shift = 0;
        int m_mem_port_id = -1;   // an LLC bank's upper (memory-side) endpoint id on a network (spec s25.1); -1 = none
        int  homeOf(uint64_t addr) const;
        bool isHome(int id) const;

        // Perfect-LLC knob (default 0 = off). When 1, this controller never issues
        // to the memory above it: a miss's fetch is synthesized locally and looped
        // back as an upper-interface data response (no bus transport, no DRAM
        // latency), and a dirty eviction's write-back to memory is dropped. Set
        // only on the LLC (llc_controller.perfect_llc). Models the "perfect LLC"
        // assumption of predictable-coherence analyses. Allocation/eviction are
        // unchanged, so a perfect-vs-real run isolates exactly the DRAM leg.
        int m_perfect_llc;

        // Design B logging role for this controller (L1 by default; the LLC is
        // tagged via setLogRole). Used to stamp self-describing timeline events.
        Logger::Role m_log_role = Logger::Role::L1;

        uint64_t m_cache_cycle;

        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO
        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO

        CacheDataHandler *m_data_handler;              // A pointer to Private cache //Data Handler
        CoherenceProtocolHandler *m_protocol; // A pointer to Cache Coherence Protocol

        // This queue is used mainly to serialize messages that come from different sources
        FRFCFS_Buffer<Message, CoherenceProtocolHandler> *m_processing_queue;
        int m_processing_queue_size;   // demand admission bound of that queue (-1 = none)
        // Set when an external core model (the gem5 bridge) drives this L1.
        // Receives the commit of each CPU request and the loss of any
        // readable line; NULL in the standalone simulator.
        ExternalCPU *m_cpu_port = NULL;
        uint64_t m_data_read_failures = 0;   // responses built without line data
        // Occupancy diagnostics, reported at the end of a run (reportOccupancy):
        // cycles a popped request was turned away by canAdmitRequest (MSHR or
        // write-back buffer full), interconnect intakes refused because the
        // demand queue was at its bound, and the queue's peak occupancy.
        uint64_t m_admit_fail_cycles = 0;
        uint64_t m_intake_refusals = 0;
        int m_queue_peak = 0;
        static std::vector<BaseController *> s_controllers;   // every controller built, for the report

        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is vector of Messages
        // to ensure order of requests of the same cache line
        std::map<uint64_t, std::vector<Message>> m_pending_requests;

        //An array of action functions of this class an its derived ones
        std::vector<std::function<void(void*)>> action_functions;

        DebugPrint* dprint;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);
        // after a lower-interface message is admitted to the queue: the LLC_QUEUE trace record and
        // the LLC mechanism trackers (arrival state, per-line gate); every intake calls it
        void noteQueueArrival(const Message &msg, FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf);

        // Diagnostics: OCTOPUS_HANG_DUMP=<cycles> dumps this controller's queues to
        // stderr once it has processed nothing for that many cycles (deadlock triage).
        uint64_t m_last_progress_cycle = 0;
        bool m_hang_dumped = false;
        [[noreturn]] void pushRefused(const char *where, const Message &msg);
        virtual void dumpState();

        // Read-after-write to memory (docs/NoC_Memory_Plan.md s2): a line written to memory is
        // kept, with its data, from the write until memory acknowledges it (K_MEM_WACK), and a
        // memory read of that line is filled from here instead of being sent. Holds for any
        // interconnect; the entries count against the write-back buffer.
        struct MemWrite { int count = 0; std::vector<uint8_t> data; };
        std::map<uint64_t, MemWrite> m_mem_writes;   // line address -> outstanding writes
        void noteMemWrite(const Message &msg);       // a K_MEM_WRITE was pushed toward memory
        void memWriteDone(uint64_t addr);            // acknowledged, or cancelled by a rollback
        bool forwardFromMemWrite(Message *msg);      // a K_MEM_READ of a written line: fill it here
        bool consumeMemAck(const Message &msg);      // intake: true if msg was a K_MEM_WACK (popped by the caller)
        std::vector<Message> m_local_fills;          // fills made here that the upper RX could not take yet (a network RX is bounded)
        void pushLocalFill(Message &fill);

        // Structural admission gate. Returns false when a ready request must be
        // held back (e.g., the derived controller has no free MSHR/PWB entry for
        // a new miss). Default: always admit. Overridden by CacheController.
        virtual bool canAdmitRequest(Message &msg) { return true; }
        // transmit-side reservations (spec s14.2): released when the message's transition has run
        virtual void txRelease(const Message &msg) { (void)msg; }
        virtual void txAnswered(const Message &msg) { (void)msg; }   // a parked request has been answered from a fill
        // Pipelined data array: lets the derived controller take a ready
        // message off the FSM path and apply its event only once the array
        // has absorbed its bytes. Default: never.
        virtual bool deferForDataArray(Message &msg) { return false; }
        virtual void traceMsg(const char *what, const Message &msg) {}   // debug trace hook (CacheController)

        virtual uint64_t getAddressKey(uint64_t addr);

        virtual void hitAction(void *);
        virtual void removePendingAndRespond(void *);
        virtual void addtoPendingRequests(void *);
        virtual void sendBusRequest(void *);
        virtual void performWriteBack(void *);
        virtual void updateCacheLine(void *);

    public:
        BaseController(ParametersMap map, CommunicationInterface *upper_interface, 
                       CommunicationInterface *lower_interface, string pname = "",
                       string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                       string name = STRINGIFY(BaseController));
        ~BaseController();

        virtual void init();
        void setCpuPort(ExternalCPU *port) { m_cpu_port = port; }
        // One line per controller that ever refused or stalled: how close the
        // queue, MSHR and write-back bounds came to binding in this run.
        static void reportOccupancy(std::ostream &os);
        // Mirror of a classic cache's blocked CPU port. `outstanding` is the
        // number of the external core's requests accepted and not yet
        // answered. The queue bound is read as the total the core is credited
        // with: an accepted request holds a credit until its response,
        // whether it waits in this queue, in the pending table, or on its way
        // back, so every buffer between the core and this controller is
        // bounded by that same number. The L1 controller adds its MSHR and
        // write-back-buffer bounds.
        virtual bool demandAdmissionBlocked(int outstanding) const;
        // Fatal diagnostic for a response that needs line data the array no
        // longer holds (replaces a memcpy from NULL).
        void dataArrayReadFailed(const char *where, const Message *msg);
        int demandQueueSize() const { return m_processing_queue_size; }

        // Design B: tag this controller's logging role (e.g. LLC). Default is L1.
        void setLogRole(Logger::Role role) { m_log_role = role; }
        Logger::Role logRole() const { return m_log_role; }
        static const std::vector<BaseController *> &controllers() { return s_controllers; }
        CoherenceProtocolHandler *protocol() const { return m_protocol; }
        int id() const { return m_id; }

        virtual void initialize(uint64_t address, const uint8_t* data, int size) {} //for Initializable
        virtual void read(uint64_t address, uint8_t* data) {} //for Initializable
    };
}

#endif /* _BaseController_H */
