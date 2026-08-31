/*
 * File  :      CacheController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _CacheController_H
#define _CacheController_H

#include "BaseController.h"
#include "CacheDataHandler_COTS.h"

#include "Arbiter.h"
#include "RRArbiter.h"
#include "FCFSArbiter.h"

namespace octopus
{
    // Finite realistic default for the MSHR depth (max concurrent outstanding
    // misses) when no `num_mshr` is given in config. Set to -1 in config to
    // restore the previous unbounded behavior.
    #define DEFAULT_NUM_MSHR 16

    class CacheController : public BaseController
    {
    protected:
        // Max concurrent outstanding misses (MSHR depth). -1 = unbounded.
        int m_num_mshr;

        // key is the msg.addr & mask(nbits of CacheLineSize); value is a QUEUE of pending
        // requests to forward once data arrives. MOESI: a transient owner-to-be may see
        // several sharers' GetS before it has data and must forward to ALL of them.
        std::map<uint64_t, std::vector<Message>> m_saved_requests_for_wb;

        // key is the msg.m_id and the value is the Message that contains the data
        std::map<uint64_t, Message> m_modifying_data_messages;

        std::vector<Message> m_data_access_buffer;
        std::map<int, ControllerAction> m_data_access_action; // The map holds the action is required by the entry in m_data_array_queue (Key is the message id)
        Arbiter *m_data_access_arbiter;

        // Data-forward transient support (M_dS/M_dI). When an owner must read its bank to
        // forward data on a snoop, the FSM moves to a transient state (line stays valid) and
        // issues StartRead: this occupies the bank for the access latency and defers a
        // completion entry into the SHARED m_data_access_buffer (so it takes its turn via the
        // arbiter and is drained on eviction like any other pending data access). When that
        // entry is serviced -- after >=L cycles, or forced on eviction (line then living in the
        // write buffer with its transient state) -- it injects a self DataArrayReady message
        // that drives the transient to its final state and forwards the data. Coherence resolves
        // immediately; the data delay lives in the FSM, reusing the existing latency machinery.
        virtual void startTimedRead(void *);
        virtual void emitDataReady(void *);
        // When a line transitions to Invalid via a coherence action, any pending fill
        // (WRITE_CACHE_LINE_DATA) for it in m_data_access_buffer is moot -- the received data
        // was already used/forwarded and there is no point writing it into a now-invalid line
        // (and doing so would fault on the missing line). Drop those entries.
        virtual void dropPendingFills(uint64_t address);

        virtual void cycleProcess() override;
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &) override;
        virtual void processDataArrayBuffer();


        virtual void hitAction(void *) override;
        virtual void removePendingAndRespond(void *) override;
        virtual void addtoPendingRequests(void *) override;
        virtual void performWriteBack(void *) override;
        virtual void updateCacheLine(void *) override;

        virtual void writeCacheLineData(void *);
        virtual void modifyData(void *);
        virtual void saveReqForWriteBack(void *);
        virtual void noAction(void *){}; // empty function
        virtual void stall(void *);
        // Snoop LLC eviction back-invalidation. Routed via MessageType::SERVICE_REQUEST
        // on the TripleBus service channel, which broadcasts it to every interface, so
        // all L1 sharers see the INV (Invalidation) and the LLC receives its own copy
        // back (Own_Invalidation) to complete the eviction (WriteBack/N).
        virtual void sendInvalidationMessage(void *);

        virtual bool checkReadinessOfCache(Message &msg, ControllerAction::Type type, void *data_ptr);
        virtual void checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

        // Stall a brand-new demand miss when the MSHR is full (outstanding-miss
        // limit reached) or the write-back buffer (PWB) has no headroom.
        virtual bool canAdmitRequest(Message &msg) override;
        virtual int mshrLimit() override { return m_num_mshr; } // SCRATCH: deadlock dump

    public:
        CacheController(ParametersMap map, CommunicationInterface *upper_interface, 
                        CommunicationInterface *lower_interface, string pname = "",
                        string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                        string name = STRINGIFY(CacheController));
        ~CacheController();

        virtual void initializeCacheData(std::vector<std::string> &tracePaths);

        virtual void initialize(uint64_t address, const uint8_t* data, int size) override; //for Initializable
        virtual void initialize_child(uint64_t address, const uint8_t* data, int size) override; //for Initializable
        virtual void read(uint64_t address, uint8_t* data) override;

        std::vector<CacheController*> m_children_controllers;
    };
}

#endif /* _CacheController_H */
