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
#include <deque>

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

        Arbiter *m_data_access_arbiter;

        // ---- pipelined data array (m_data_handler->isPipelined(), latency > 0) ----
        // One array operation in flight. Either a message carrying bytes, whose
        // FSM event is applied only once the bytes have been written (the line
        // keeps its transient state meanwhile, so loads stall and snoops take
        // the table's SaveReq path), or an action list that must read or write
        // the array before it can run (a hit, a write-back, a store).
        struct DataArrayOp
        {
            enum Kind { MESSAGE, ACTIONS } kind;
            Message msg;                            // the message, or the parked action's message
            std::vector<ControllerAction> actions;  // ACTIONS: run in order on completion
            uint64_t op_id = 0;                     // unique; the arbiter elects a message copy carrying it
            bool admitted = false;                  // pipelined: inside the array, completes at ready_cycle
            uint64_t ready_cycle = 0;
        };
        // Level 2: every access that needs the data array, in arrival order,
        // whole deferred messages and parked single actions alike. Level 1
        // (the processing queue's hold) releases at most one access per line,
        // so this list is scheduled purely as a resource: the configured
        // arbiter (FCFS / RR / TDM, keyed on the requesting core) or oldest
        // first, within the array's ports and latency.
        std::deque<DataArrayOp> m_array_ops;
        uint32_t m_array_admitted_this_cycle = 0;   // pipelined: admissions so far this cycle
        uint64_t m_array_op_seq = 0;
        std::vector<int> m_arbiter_candidates;      // cores the arbiter schedules; others go oldest first
        bool m_pipe_firing = false;                 // completing an op: its array calls run inline
        uint64_t m_pipe_ops = 0;
        uint64_t m_pipe_early_fires = 0;            // completed early: their line was evicted
        bool lineHasDeferredOp(uint64_t address);   // address interlock: a deferred op is waiting or in flight on this line (queue hold)
        bool messageTouchesArray(const Message &msg); // this model's rule for "the transition reads or writes the array"
        uint64_t m_pipe_requeues = 0;               // fired ops whose row had become a Stall: sent back to the queue
        int m_line_interlock = 1;                   // config `line_interlock`: 0 disables the address interlock (A/B testing)

        bool pipelinedArray() const;
        void arrayEnqueue(DataArrayOp &&op);                // level-2 entry, both models
        std::deque<DataArrayOp>::iterator arrayElect();     // next waiting access by policy, or end()
        bool arbitrated(int owner) const;
        void arrayAdmit();                                  // pipelined: admit up to the ports this cycle
        void pipelineFire(DataArrayOp &op);
        void pipelineStep();                                // pipelined scheduler
        void pipelineFlushLine(uint64_t address);           // the line leaves the array: complete its ops now
        virtual bool deferForDataArray(Message &msg) override;
        virtual void removePendingAndRespond(void *) override;

        virtual void cycleProcess() override;
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &) override;
        virtual void processDataArrayBuffer();


        virtual void hitAction(void *) override;
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
        // Adds the MSHR and write-back-buffer bounds to the queue bound.
        virtual bool demandAdmissionBlocked(int outstanding) const override;

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
