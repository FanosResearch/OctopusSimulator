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
    class CacheController : public BaseController
    {
    protected:
        // key is the msg.addr & mask(nbits of CacheLineSize) and the value is request Message
        std::map<uint64_t, Message> m_saved_requests_for_wb;

        // key is the msg.m_id and the value is the Message that contains the data
        std::map<uint64_t, Message> m_modifying_data_messages;

        std::vector<Message> m_data_access_buffer;
        std::map<int, ControllerAction> m_data_access_action; // The map holds the action is required by the entry in m_data_array_queue (Key is the message id)
        Arbiter *m_data_access_arbiter;

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

        virtual bool checkReadinessOfCache(Message &msg, ControllerAction::Type type, void *data_ptr);
        virtual void checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &);

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
