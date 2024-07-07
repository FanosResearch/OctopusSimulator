/*
 * File  :      MSIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/Protocols/MSIDirectory.h"
using namespace std;

namespace octopus
{
    MSIDirectory::MSIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : DirectoryProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    MSIDirectory::~MSIDirectory()
    {
    }

    FRFCFS_State MSIDirectory::getRequestState(const Message &msg, FRFCFS_State req_state)
    {
        GenericCacheLine cache_line;
        EventId event_id;
        Message message = msg;

        m_data_handler->readLineBits(message.addr, &cache_line);
        this->readEvent(message, &event_id);

        if (this->m_fsm->isStall(cache_line.state, (int)event_id))
            return FRFCFS_State::NonReady;

        return FRFCFS_State::Ready;
    }

    vector<ControllerAction> MSIDirectory::processRequest(Message &request_msg, DebugPrint* dprint)
    {
        EventId event_id;
        int next_state;
        vector<int> actions;
        GenericCacheLine cache_line;
        vector<ControllerAction> controller_actions;

        m_data_handler->readLineBits(request_msg.addr, &cache_line);

        this->readEvent(request_msg, &event_id);
        this->m_fsm->getTransition(cache_line.state, (int)event_id, next_state, actions);

        if(dprint)
            dprint->print(&request_msg, "state(%d) to nextState(%d) due to event(%d) and num of actions = %d", 
                            cache_line.state, next_state, (int)event_id, actions.size());

        return handleAction(actions, request_msg, cache_line, next_state);
    }

    vector<ControllerAction> MSIDirectory::handleAction(vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        vector<ControllerAction> controller_actions;

        if (!((actions.size() > 0) && (actions[0] == (int)ActionId::Stall))) //Skip cache line update if the action is stall
        {
            // update cache line
            ControllerAction controller_action;

            cache_line.valid = this->m_fsm->isValidState(next_state);
            cache_line.state = next_state;

            controller_action.type = ControllerAction::Type::UPDATE_CACHE_LINE;
            controller_action.data = (void *)new uint8_t[sizeof(Message) + sizeof(cache_line)];

            new (controller_action.data) Message(msg);
            new ((uint8_t *)controller_action.data + sizeof(Message)) GenericCacheLine(cache_line);

            controller_actions.push_back(controller_action);
        }

        for (int action : actions)
        {
            ControllerAction controller_action;
            switch (static_cast<ActionId>(action))
            {
            case ActionId::Stall:
                controller_action.type = ControllerAction::Type::STALL;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                std::cout << " MSIDirectory: Stall Transaction is detected" << std::endl;
                // exit(0);
                break;

            case ActionId::Hit: // remove request from pending and respond to cpu, update cache line
            {
                if((msg.source == Message::Source::LOWER_INTERCONNECT) || (msg.data == NULL))
                {
                    if(msg.source == Message::Source::UPPER_INTERCONNECT)
                    {
                        controller_action.type = ControllerAction::Type::REMOVE_PENDING_NORESPONSE;
                        controller_action.data = (void *)new Message(msg);
                        controller_actions.push_back(controller_action);
                    }    
                    controller_action.type = ControllerAction::Type::HIT_Action;
                }
                else
                    controller_action.type = ControllerAction::Type::REMOVE_PENDING;
                    
                controller_action.data = (void *)new Message(msg);
                controller_actions.push_back(controller_action);

                uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
                if((msg.source == Message::Source::UPPER_INTERCONNECT) &&
                   (ack_counters->find(addr_key) != ack_counters->end()) &&
                   ((ack_counters->at(addr_key) + msg.complementary_value) == 0))
                {
                    controller_action.type = ControllerAction::Type::SET_ACK_NUMBER;
                    controller_action.data = (void *)new Message(msg);
                    controller_actions.push_back(controller_action);
                }

                controller_action.type = ControllerAction::Type::MODIFY_DATA;
                controller_action.data = (void *)new Message(msg);
            }
                break;

            case ActionId::GetS:
            case ActionId::GetM:
                // add request to pending requests
                controller_action.type = ControllerAction::Type::ADD_PENDING;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                controller_actions.push_back(controller_action);

                // send Bus request
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,                                                        // Id
                                                             msg.addr,                                                          // Addr
                                                             0,                                                                 // Cycle
                                                             (action == (int)ActionId::GetS) ? MSIDirectory::REQUEST_TYPE_GETS
                                                                                             : MSIDirectory::REQUEST_TYPE_GETM, // Complementary_value
                                                             (uint16_t)this->m_id);                                             // Owner

                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                break;
            case ActionId::PutS:
                // send Bus request
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,                     // Id
                                                             msg.addr,                       // Addr
                                                             0,                              // Cycle
                                                             MSIDirectory::REQUEST_TYPE_PUTS,// Complementary_value
                                                             (uint16_t)this->m_id);          // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                break;
            case ActionId::PutM_Data:
            {
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = new Message(msg.msg_id,                        // Id
                                                     msg.addr,                          // Addr
                                                     0,                                 // Cycle
                                                     MSIDirectory::REQUEST_TYPE_PUTM,   // Complementary_value
                                                     (uint16_t)this->m_id);             // Owner
                
                ((Message *)controller_action.data)->to.clear();                
                controller_actions.insert(controller_actions.begin(), controller_action);
                continue; //skip push back at the end of the loop
            }

            case ActionId::Data2Req:
            case ActionId::Data2Both:
                // Do writeback
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = (void *)new Message(msg);
                
                ((Message *)controller_action.data)->complementary_value = 0;
                ((Message *)controller_action.data)->to.clear();
                if (action == (int)ActionId::Data2Both)
                    ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                
                controller_actions.insert(controller_actions.begin(), controller_action);
                continue; //skip push back at the end of the loop
                
            case ActionId::InvAck2Req:
                // send Bus request
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,                        // Id
                                                             msg.addr,                          // Addr
                                                             0,                                 // Cycle
                                                             MSIDirectory::REQUEST_TYPE_INV_ACK,// Complementary_value
                                                             (uint16_t)this->m_id);             // Owner
                ((Message *)controller_action.data)->to.push_back(msg.owner);
                break;
            case ActionId::InvAck_Data:
            {
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = new Message(msg.msg_id,                        // Id
                                                     msg.addr,                          // Addr
                                                     0,                                 // Cycle
                                                     MSIDirectory::REQUEST_TYPE_INV_ACK,// Complementary_value
                                                     (uint16_t)this->m_id);             // Owner
                
                ((Message *)controller_action.data)->to.clear();                
                controller_actions.insert(controller_actions.begin(), controller_action);

                continue; //skip push back at the end of the loop
            }
            
            case ActionId::Ack_dec:                
                controller_action.type = ControllerAction::Type::DECREMENT_ACK;
                controller_action.data = (void *)new Message(msg);
                break;

            case ActionId::AckNum_set:                
                controller_action.type = ControllerAction::Type::SET_ACK_NUMBER;
                controller_action.data = (void *)new Message(msg);
                break;

            case ActionId::SaveReq:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SAVE_REQ_FOR_WRITE_BACK;
                controller_action.data = (void *)new Message(msg.msg_id, // Id
                                                             msg.addr,   // Addr
                                                             0,          // Cycle
                                                             0,          // Complementary_value
                                                             msg.owner); // Owner
                break;

            case ActionId::Fault:
                std::cout << " MSIDirectory: Fault Transaction is detected" << std::endl;
                exit(0);
                break;
            }

            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }

    void MSIDirectory::readEvent(Message &msg, EventId *out_id)
    {
        switch (msg.source)
        {
        case Message::Source::LOWER_INTERCONNECT:
            *out_id = (msg.complementary_value == 0) ? EventId::Load : EventId::Store;
            return;
        
        case Message::Source::SELF:
            if (msg.owner == m_id)
            {    
                *out_id = EventId::Replacement;
                return;
            }
            else
            {
                std::cout << " LLCMSIDirectory: Invalid Transaction" << std::endl;
                exit(0);
            }   
        
        case Message::Source::UPPER_INTERCONNECT:
            if (msg.data != NULL)
            {
                if(msg.from != m_shared_memory_id)
                    *out_id = EventId::DataFromOwner;
                else if(msg.complementary_value == 0)
                    *out_id = EventId::Data_zeroAck;
                else if(msg.complementary_value > 0)
                {
                    uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
                    if((ack_counters->find(addr_key) != ack_counters->end()) &&
                       ((ack_counters->at(addr_key) + msg.complementary_value) == 0))
                        *out_id = EventId::Data_zeroAck;
                    else
                        *out_id = EventId::Data_nonZeroAck;
                }
                else
                {
                    std::cout << " MSIDirectory: Invalid Transaction detected" << std::endl;
                    exit(0);
                }
            }
            else
            {
                switch (msg.complementary_value)
                {
                case MSIDirectory::REQUEST_TYPE_GETS:
                    *out_id = EventId::Fwd_GetS;
                    return;
                case MSIDirectory::REQUEST_TYPE_GETM:
                    *out_id = EventId::Fwd_GetM;
                    return;
                case MSIDirectory::REQUEST_TYPE_PUT_ACK:
                    *out_id = EventId::Put_Ack;
                    return;
                case MSIDirectory::REQUEST_TYPE_INV:
                    *out_id = EventId::Inv;
                    return;
                case MSIDirectory::REQUEST_TYPE_INV_ACK:
                {
                    uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
                    if((ack_counters->find(addr_key) != ack_counters->end()) && ((ack_counters->at(addr_key) - 1) == 0))
                        *out_id = EventId::lastInvAck;
                    else
                        *out_id = EventId::InvAck;
                    return;
                }
                default: // Invalid Transaction
                    std::cout << " MSIDirectory: Invalid Transaction detected" << std::endl;
                    exit(0);
                }
            }
            return;

        default:
            std::cout << "Invalid message source" << std::endl;
        }
    }
}