/*
 * File  :      CacheController_End2End.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 22, 2022
 */

#include "../../header/CacheControllers/CacheController_End2End.h"

namespace octopus
{
    // private controller constructor
    CacheController_End2End::CacheController_End2End(ParametersMap map, CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                                                     string pname, string config_path, string name)
        : CacheController(map, upper_interface, lower_interface, pname, config_path, name)
    {
        m_owner_of_latest_data = -1;
        
        action_functions[ControllerAction::Type::SEND_INV_MSG] = [&](void* ptr) {this->sendInvalidationMessage(ptr);};
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

    void CacheController_End2End::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message returned_msg;

        if(m_upper_interface->rollback(msg->addr, m_data_handler->getBlockSize(), &returned_msg))
        {
            if(returned_msg.data != NULL)
            {
                msg->copy(returned_msg.data);
                m_upper_interface->pushMessage2RX(*msg, MessageType::DATA_RESPONSE);
            }
            else
            {
                cout << "CacheController_End2End(id = " << this->m_id << "): Wrong message returned from the rollback" << endl;
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
        
        msg->owner = (m_owner_of_latest_data > -1) ? m_owner_of_latest_data : this->m_id;
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
            cout << "CacheController_End2End(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController_End2End::initialize(uint64_t address, const uint8_t* data, int size) //for Initializable
    {
            CacheController::initialize(address, data, size);
    }
}