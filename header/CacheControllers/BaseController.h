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

        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is vector of Messages
        // to ensure order of requests of the same cache line
        std::map<uint64_t, std::vector<Message>> m_pending_requests;

        //An array of action functions of this class an its derived ones
        std::vector<std::function<void(void*)>> action_functions;

        DebugPrint* dprint;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

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

        virtual void initialize(uint64_t address, const uint8_t* data, int size) {} //for Initializable
        virtual void read(uint64_t address, uint8_t* data) {} //for Initializable
    };
}

#endif /* _BaseController_H */
