/*
 * File  :      CacheController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _CacheController_H
#define _CacheController_H

#include "CommunicationInterface.h"
#include "CacheDataHandler.h"
#include "CacheDataHandler_COTS.h"
#include "CacheXml.h"
#include "ClockManager.h"
#include "Initializable.h"

#include "Protocols.h"
#include "FRFCFS_Buffer.h"
#include "Logger.h"
#include "Arbiter.h"
#include "RRArbiter.h"

#include "Policy.h"

#include <string>
#include <queue>
#include <vector>
#include <map>

namespace ns3
{
    class CacheController : public ClockedObj, public Initializable
    {
    protected:
        int m_core_id;
        int m_shared_memory_id;
        int m_cache_line_size;

        double m_dt;
        double m_clk_skew;
        uint64_t m_cache_cycle;

        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO
        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO

        CacheDataHandler *m_data_handler;              // A pointer to Private cache //Data Handler
        CoherenceProtocolHandler *m_protocol; // A pointer to Cache Coherence Protocol

        // This queue is used mainly to serialize messages that come from different sources
        FRFCFS_Buffer<Message, CoherenceProtocolHandler> *m_processing_queue;

        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is queue of Messages
        // to ensure order of requests of the same cache line
        std::map<uint64_t, std::vector<Message>> m_pending_cpu_requests;

        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is request Message
        std::map<uint64_t, Message> m_saved_requests_for_wb;

        // key is the msg.m_id and the value is the Message that contains the data
        std::map<uint64_t, Message> m_modifying_data_messages;

        std::vector<Message> m_data_access_buffer;
        std::map<int, ControllerAction> m_data_access_action; // The map holds the action is required by the entry in m_data_array_queue (Key is the message id)
        Arbiter *m_data_access_arbiter;


        virtual void cycleProcess();
        virtual void processLogic();
        virtual void processDataArrayBuffer();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

        virtual uint64_t getAddressKey(uint64_t addr);

        virtual void callActionFunction(ControllerAction);

        virtual void removePendingAndRespond(void *);
        virtual void hitAction(void *);
        virtual void addtoPendingRequests(void *);
        virtual void sendBusRequest(void *);
        virtual void performWriteBack(void *);
        virtual void updateCacheLine(void *);
        virtual void writeCacheLineData(void *);
        virtual void modifyData(void *);
        virtual void saveReqForWriteBack(void *);        
        virtual void sendInvalidationMessage(void *);
        virtual void noAction(void *){}; // empty function
        virtual void stall(void *);

        virtual bool checkReadinessOfCache(Message &msg, ControllerAction::Type type, void *data_ptr);
        virtual void checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

    public:
        CacheController(CacheXml &cacheXml, string &fsm_path,
                        CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                        bool cach2Cache, int sharedMemId, CohProtType pType, vector<int>* private_caches_id = NULL);
        ~CacheController();

        virtual void init();
        virtual void initializeCacheData(std::vector<std::string> &tracePaths);

        virtual void initialize(uint64_t address, const uint8_t* data, int size); //for Initializable
        virtual void initialize_2(uint64_t address, const uint8_t* data, int size); //for Initializable
        virtual void read(uint64_t address, uint8_t* data);

        int getId() {return m_core_id;}

        std::vector<CacheController*> m_children_controllers;
    };
}

#endif /* _CacheController_H */
