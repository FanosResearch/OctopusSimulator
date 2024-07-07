/*
 * File  :      LLCMSIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 2, 2023
 */

#include "../../header/Protocols/LLCMSIDirectory.h"
using namespace std;

namespace octopus
{
    LLCMSIDirectory::LLCMSIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : DirectoryProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    LLCMSIDirectory::~LLCMSIDirectory()
    {
    }

    FRFCFS_State LLCMSIDirectory::getRequestState(const Message &msg, FRFCFS_State req_state)
    {
        GenericCacheLine cache_line;
        EventId event_id;
        
        m_data_handler->readLineBits(msg.addr, &cache_line);
        this->readEvent((Message&)msg, cache_line, &event_id);

        if (msg.data != NULL)
            return FRFCFS_State::Ready;
        else if (this->m_fsm->isStall(cache_line.state, (int)event_id))
            return FRFCFS_State::NonReady;

        return FRFCFS_State::Ready;
    }

    vector<ControllerAction> LLCMSIDirectory::processRequest(Message &request_msg, DebugPrint* dprint)
    {
        GenericCacheLine cache_line;
        EventId event_id;
        int next_state;
        vector<int> actions;

        m_data_handler->readLineBits(request_msg.addr, &cache_line);

        this->readEvent(request_msg, cache_line, &event_id);
        this->m_fsm->getTransition(cache_line.state, (int)event_id, next_state, actions);

        if(dprint)
            dprint->print(&request_msg, "state(%d) to nextState(%d) due to event(%d) and num of actions = %d", 
                            cache_line.state, next_state, (int)event_id, actions.size());

        return handleAction(actions, request_msg, cache_line, next_state);
    }

    vector<ControllerAction> LLCMSIDirectory::handleAction(std::vector<int> &actions, Message &msg,
                                                           GenericCacheLine &cache_line, int next_state)
    {
        std::vector<ControllerAction> controller_actions;
        int old_owner = cache_line.owner_id;
            
        for (int action : actions)
        {
            ControllerAction controller_action;
            switch (static_cast<ActionId>(action))
            {
            case ActionId::Stall:
                std::cout << " LLCMSIDirectory: Stall Transaction is detected" << std::endl;
                exit(0);
                break;

            case ActionId::GetData:
                // add request to pending requests
                controller_action.type = ControllerAction::Type::ADD_PENDING;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                controller_actions.push_back(controller_action);

                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,       // Id
                                                             msg.addr,         // Addr
                                                             0,                // Cycle
                                                             (uint16_t)action, // Complementary_value
                                                             msg.owner);       // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                break;
            case ActionId::SendData: // remove request from pending and respond to request
            {
                controller_action.type = (msg.source == Message::Source::LOWER_INTERCONNECT)
                                             ? ControllerAction::Type::HIT_Action
                                             : ControllerAction::Type::REMOVE_PENDING;
                
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                ((Message *)controller_action.data)->to.clear();
                ((Message *)controller_action.data)->to.push_back(msg.owner);

                if(cache_line.owner_id != -1)
                {
                    uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
                    if(protocol_counters->find(addr_key) != protocol_counters->end())
                    {
                        auto list = protocol_counters->at(addr_key);
                        ((Message *)controller_action.data)->complementary_value = list.size();//add number of sharers to the data response

                        if(find(list.begin(), list.end(), msg.owner) != list.end()) //check if the requestor is one of the sharers
                            ((Message *)controller_action.data)->complementary_value--;
                    }
                    else
                        ((Message *)controller_action.data)->complementary_value = 0;
                }
                else
                   ((Message *)controller_action.data)->complementary_value = 0; 
            }
                break;

            case ActionId::SaveData: // update cacheline is added by default if Action is not Stall
                controller_action.type = ControllerAction::Type::NO_ACTION;
                break;
            case ActionId::SetOwner:
                cache_line.owner_id = msg.owner;
                controller_action.type = ControllerAction::Type::SET_SHARERS;
                controller_action.data = (void *)new Message(msg);
                break;
            case ActionId::ClearOwner:
                cache_line.owner_id = -1;
                controller_action.type = ControllerAction::Type::NO_ACTION;//CLEAR_SHARERS;
                controller_action.data = (void *)new Message(msg);
                break;

            case ActionId::SendInv:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
                controller_action.data = (void *)new Message(msg);

                ((Message *)controller_action.data)->complementary_value = (uint16_t)MSIDirectory::REQUEST_TYPE_INV;

                ((Message *)controller_action.data)->to.clear();
                for(uint64_t id : protocol_counters->at(msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1)))
                {
                    if(id == msg.owner) //skip invalidation for the message owner
                        continue;
                    ((Message *)controller_action.data)->to.push_back((uint16_t)id);
                }
                break;
            case ActionId::FwdGetS:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
                controller_action.data = (void *)new Message(msg);
                
                ((Message *)controller_action.data)->complementary_value = (uint16_t)MSIDirectory::REQUEST_TYPE_GETS;

                ((Message *)controller_action.data)->to.clear();
                ((Message *)controller_action.data)->to.push_back(old_owner);
                // for(uint64_t id : protocol_counters->at(msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1)))
                //     ((Message *)controller_action.data)->to.push_back((uint16_t)id);
                break;
            case ActionId::FwdGetM:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
                controller_action.data = (void *)new Message(msg);
                
                ((Message *)controller_action.data)->complementary_value = (uint16_t)MSIDirectory::REQUEST_TYPE_GETM;

                ((Message *)controller_action.data)->to.clear();
                ((Message *)controller_action.data)->to.push_back(old_owner);
                // for(uint64_t id : protocol_counters->at(msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1)))
                //     ((Message *)controller_action.data)->to.push_back((uint16_t)id);
                break;
            case ActionId::PutAck:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
                controller_action.data = (void *)new Message(msg.msg_id,                                    // Id
                                                             msg.addr,                                      // Addr
                                                             0,                                             // Cycle
                                                             (uint16_t)MSIDirectory::REQUEST_TYPE_PUT_ACK, // Complementary_value
                                                             (uint16_t)this->m_id);                         // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)msg.owner);
                break;

            case ActionId::WriteBack:
                // Do writeback, update cache line
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = (void *)new Message(msg.msg_id, // Id
                                                             msg.addr,   // Addr
                                                             0,          // Cycle
                                                             0,          // Complementary_value
                                                             m_id); // Owner
                break;

            case ActionId::IncSharers:
                controller_action.type = ControllerAction::Type::INCREMENT_SHARERS;
                controller_action.data = (void *)new Message(msg);
                break;
            case ActionId::DecSharers:
                controller_action.type = ControllerAction::Type::DECREMENT_SHARERS;
                controller_action.data = (void *)new Message(msg);
                break;

            case ActionId::Fault:
                std::cout << " LLCMSIDirectory: Fault Transaction is detected" << std::endl;
                exit(0);
                break;
            }

            controller_actions.push_back(controller_action);
        }

        // update cache line
        ControllerAction controller_action;

        cache_line.valid = this->m_fsm->isValidState(next_state);
        cache_line.state = next_state;

        controller_action.type = ControllerAction::Type::UPDATE_CACHE_LINE;
        controller_action.data = (void *)new uint8_t[sizeof(Message) + sizeof(cache_line)];

        new (controller_action.data) Message(msg);
        new ((uint8_t *)controller_action.data + sizeof(Message)) GenericCacheLine(cache_line);

        controller_actions.push_back(controller_action);

        return controller_actions;
    }

    void LLCMSIDirectory::readEvent(Message &msg, GenericCacheLine &cache_line, EventId *out_id)
    {
        switch (msg.source)
        {
        case Message::Source::UPPER_INTERCONNECT:
            if (msg.data != NULL)
                *out_id = EventId::Data_fromUpperInterface;
            else
            {
                std::cout << " LLCMSIDirectory: Invalid Transaction" << std::endl;
                exit(0);
            }
            break;
            
        case Message::Source::LOWER_INTERCONNECT:
            if (msg.data != NULL)
            {
                if(msg.complementary_value == MSIDirectory::REQUEST_TYPE_PUTM)
                {
                    if (msg.owner == cache_line.owner_id)
                        *out_id = EventId::PutM_Data_fromOwner;
                    else
                        *out_id = EventId::PutM_Data_fromNonOwner;
                }
                else
                    *out_id = EventId::Data_fromLowerInterface;
            }
            else
            {
                switch (msg.complementary_value)
                {
                case MSIDirectory::REQUEST_TYPE_GETS:
                    *out_id = EventId::GetS;
                    break;
                case MSIDirectory::REQUEST_TYPE_GETM:
                    *out_id = EventId::GetM;
                    break;
                case MSIDirectory::REQUEST_TYPE_PUTS:
                {
                    uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);

                    if((protocol_counters->find(addr_key) != protocol_counters->end()) && ((protocol_counters->at(addr_key).size() - 1) == 0))
                        *out_id = EventId::last_PutS;
                    else
                        *out_id = EventId::PutS;

                    break;
                }
                case MSIDirectory::REQUEST_TYPE_INV_ACK:
                {
                    uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);

                    if((protocol_counters->find(addr_key) != protocol_counters->end()) && ((protocol_counters->at(addr_key).size() - 1) == 0))
                        *out_id = EventId::last_InvAck;
                    else
                        *out_id = EventId::InvAck;

                    break;
                }
                default: // Invalid Transaction
                    std::cout << " LLCMSIDirectory: Invalid Transaction" << std::endl;
                    exit(0);
                }
            }
            break;
        
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


        default:
            std::cout << "Invalid Message Source at LLC" << std::endl;
        }
    }

    void LLCMSIDirectory::createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line)
    {
        int state = this->m_fsm->getState(string("I"));

        m_data_handler->initializeCacheLine(cache_line);
        cache_line->state = state;
        cache_line->valid = true;
    }
}