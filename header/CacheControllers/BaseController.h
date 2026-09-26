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

        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is vector of Messages
        // to ensure order of requests of the same cache line
        std::map<uint64_t, std::vector<Message>> m_pending_requests;

        //An array of action functions of this class an its derived ones
        std::vector<std::function<void(void*)>> action_functions;

        DebugPrint* dprint;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

        // Structural admission gate. Returns false when a ready request must be
        // held back (e.g., the derived controller has no free MSHR/PWB entry for
        // a new miss). Default: always admit. Overridden by CacheController.
        virtual bool canAdmitRequest(Message &msg) { return true; }
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

        virtual void initialize(uint64_t address, const uint8_t* data, int size) {} //for Initializable
        virtual void read(uint64_t address, uint8_t* data) {} //for Initializable
    };
}

#endif /* _BaseController_H */
