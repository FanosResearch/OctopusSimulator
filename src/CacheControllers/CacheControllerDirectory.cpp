/*
 * File  :      CacheControllerDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 10, 2023
 */

#include "../../header/CacheControllers/CacheControllerDirectory.h"

namespace octopus
{
    CacheControllerDirectory::CacheControllerDirectory(ParametersMap map, CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                                                       string pname, string config_path, string name) 
    : CacheController(map, upper_interface, lower_interface, pname, config_path, name)
    {
        ((DirectoryProtocol*)m_protocol)->setProtocolCounters(&m_sharers);
        ((DirectoryProtocol*)m_protocol)->setACKCounters(&m_ack_counters);

        action_functions[ControllerAction::Type::ADD_DATA2MSG] = [&](void* ptr) {addData2Message(ptr);};
        action_functions[ControllerAction::Type::SET_ACK_NUMBER] = [&](void* ptr) {setACKNumber(ptr);};
        action_functions[ControllerAction::Type::DECREMENT_ACK] = [&](void* ptr) {decrementACK(ptr);};
        action_functions[ControllerAction::Type::INCREMENT_SHARERS] = [&](void* ptr) {incrementSharers(ptr);};
        action_functions[ControllerAction::Type::DECREMENT_SHARERS] = [&](void* ptr) {decrementSharers(ptr);};
        action_functions[ControllerAction::Type::SET_SHARERS] = [&](void* ptr) {setSharers(ptr);};
        action_functions[ControllerAction::Type::CLEAR_SHARERS] = [&](void* ptr) {clearSharers(ptr);};
        action_functions[ControllerAction::Type::SEND_FWD_MESSAGE] = [&](void* ptr) {sendForwardMessage(ptr);};
        action_functions[ControllerAction::Type::REMOVE_PENDING_NORESPONSE] = [&](void* ptr) {removePendingNoResponse(ptr);};
    }
    
    void CacheControllerDirectory::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            if(find(msg.to.begin(), msg.to.end(), m_id) == msg.to.end()) //check if this message is destined to this controller
                m_upper_interface->popFrontMessage();
            else
            {
                msg.source = Message::Source::UPPER_INTERCONNECT;
                FRFCFS_State state = m_protocol->getRequestState(msg, FRFCFS_State::NonReady);
                if ((msg.data != NULL) && buf.pushFront(msg))
                    m_upper_interface->popFrontMessage();
                else if(buf.pushBack(msg, state))
                    m_upper_interface->popFrontMessage();
            }
        }

        if (m_lower_interface->peekMessage(&msg))
        {
            if(find(msg.to.begin(), msg.to.end(), m_id) == msg.to.end()) //check if this message is destined to this controller
                m_lower_interface->popFrontMessage();
            else
            {
                msg.source = Message::Source::LOWER_INTERCONNECT;
                if (buf.pushBack(msg, FRFCFS_State::NonReady))
                    m_lower_interface->popFrontMessage();
            }
        }

        this->checkReplacements(buf);
    }

    void CacheControllerDirectory::addData2Message(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
 
        GenericCacheLine cache_line;
        m_data_handler->readCacheLine(msg->addr, &cache_line);
        msg->copy(cache_line.m_data);
    
        // Message should not be deleted as it's shared with another controller action
    }

    void CacheControllerDirectory::setACKNumber(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(m_ack_counters.find(this->getAddressKey(msg->addr)) == m_ack_counters.end())
            m_ack_counters[this->getAddressKey(msg->addr)] = 0;

        m_ack_counters[this->getAddressKey(msg->addr)] += msg->complementary_value;

        dprint->print(msg, "setAct to %d", m_ack_counters[this->getAddressKey(msg->addr)]);
        
        if(m_ack_counters[this->getAddressKey(msg->addr)] == 0)
            m_ack_counters.erase(this->getAddressKey(msg->addr));
        delete msg;
    }

    void CacheControllerDirectory::decrementACK(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(m_ack_counters.find(this->getAddressKey(msg->addr)) == m_ack_counters.end())
            m_ack_counters[this->getAddressKey(msg->addr)] = 0;

        m_ack_counters[this->getAddressKey(msg->addr)]--;

        dprint->print(msg, "decAct to %d", m_ack_counters[this->getAddressKey(msg->addr)]);

        if(m_ack_counters[this->getAddressKey(msg->addr)] == 0)
            m_ack_counters.erase(this->getAddressKey(msg->addr));
        delete msg;
    }

    void CacheControllerDirectory::incrementSharers(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        m_sharers[this->getAddressKey(msg->addr)].push_back(msg->owner);
        delete msg;
    }

    void CacheControllerDirectory::decrementSharers(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        auto iter = find(m_sharers[this->getAddressKey(msg->addr)].begin(),
                         m_sharers[this->getAddressKey(msg->addr)].end(), msg->owner);
        if(iter !=  m_sharers[this->getAddressKey(msg->addr)].end())
        {
            m_sharers[this->getAddressKey(msg->addr)].erase(iter);
            
            if(m_sharers[this->getAddressKey(msg->addr)].size() == 0)
                m_sharers.erase(this->getAddressKey(msg->addr));
        }
        delete msg;
    }

    void CacheControllerDirectory::setSharers(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        m_sharers[this->getAddressKey(msg->addr)].clear();
        m_sharers[this->getAddressKey(msg->addr)].push_back(msg->owner);
        delete msg;
    }

    void CacheControllerDirectory::clearSharers(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        m_sharers[this->getAddressKey(msg->addr)].clear();
        m_sharers.erase(this->getAddressKey(msg->addr));
        delete msg;
    }

    void CacheControllerDirectory::sendForwardMessage(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        if(!msg->to.empty())
        {
            if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::SERVICE_REQUEST))
            {
                cout << "CacheController(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
        }

        delete msg;
    }

    void CacheControllerDirectory::removePendingNoResponse(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (m_pending_requests.find(getAddressKey(msg->addr)) != m_pending_requests.end())
            this->m_pending_requests.erase(this->getAddressKey(msg->addr));
        else
        { 
            cout << "CacheControllerDirectory: Request is not found in the pending buffer." << endl;
            exit(0);
        }

        delete msg;
    }
}