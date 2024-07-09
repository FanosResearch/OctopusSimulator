/*
 * File  :      CacheController_End2End.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 22, 2022
 */

#include "../header/CacheController_End2End.h"

namespace ns3
{
    // private controller constructor
    CacheController_End2End::CacheController_End2End(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                                     int sharedMemId, CohProtType pType, vector<int> *private_caches_id)
        : CacheController(cacheXml, fsm_path, upper_interface, lower_interface, cach2Cache, sharedMemId, pType, private_caches_id)
    {
        m_owner_of_latest_data = -1;
    }

    CacheController_End2End::~CacheController_End2End()
    {
    }

    void CacheController_End2End::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            if(msg.data != NULL)
                m_owner_of_latest_data = msg.owner;
        }

        CacheController::addRequests2ProcessingQueue(buf);
    }

    void CacheController_End2End::callActionFunction(ControllerAction action)
    {
        switch (action.type)
        {
            case ControllerAction::Type::SEND_INV_MSG: this->sendInvalidationMessage(action.data); return;

            default: CacheController::callActionFunction(action); return;
        }
    }

    void CacheController_End2End::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message returned_msg;

        if(m_upper_interface->rollback(msg->addr, this->m_cache_line_size, &returned_msg))
        {
            if(returned_msg.data != NULL)
            {
                msg->copy(returned_msg.data);
                m_upper_interface->pushMessage2RX(*msg, MessageType::DATA_RESPONSE);
            }
            else
            {
                cout << "CacheController_End2End(id = " << this->m_core_id << "): Wrong message returned from the rollback" << endl;
                exit(0);
            }
            delete msg;
        }
        else
        {
            CacheController::sendBusRequest(data_ptr);
        }
    }

    void CacheController_End2End::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if(msg->data == NULL)
        {
            if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }
        
        msg->owner = (m_owner_of_latest_data > -1) ? m_owner_of_latest_data : this->m_core_id;
        msg->to.push_back(this->m_shared_memory_id);

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into BusTxResp FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }
    
    void CacheController_End2End::sendInvalidationMessage(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::SERVICE_REQUEST))
        {
            cout << "CacheController_End2End(id = " << this->m_core_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController_End2End::initialize(uint64_t address, const uint8_t* data, int size) //for Initializable
    {
        // GenericCacheLine cache_line;
        // if (this->m_data_handler->readLineBits(address, &cache_line) && this->m_cache_cycle > 1)
        // {
        //     this->m_data_handler->modifyData(address, data, size, true);
            
        //     // send Bus request, update cache line
        //     Message *msg = new Message(IdGenerator::nextReqId(),                        // Id
        //                                address,                                         // Addr
        //                                0,                                               // Cycle
        //                                (uint16_t)MSIProtocol::REQUEST_TYPE_SILENT_INV,  // Complementary_value
        //                                (uint16_t)this->m_core_id);                      // Owner
        //     msg->to.push_back((uint16_t)this->m_core_id);
        //     msg->source = Message::Source::UPPER_INTERCONNECT;
        //     this->sendInvalidationMessage((void*)msg);
        // }
        // else
            CacheController::initialize(address, data, size);
    }
}