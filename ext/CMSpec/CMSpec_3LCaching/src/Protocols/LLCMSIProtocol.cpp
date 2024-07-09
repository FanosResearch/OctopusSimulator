/*
 * File  :      LLCMSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 25, 2021
 */

#include "../../header/Protocols/LLCMSIProtocol.h"
using namespace std;

namespace ns3
{
    LLCMSIProtocol::LLCMSIProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : CoherenceProtocolHandler(cache, fsm_path, coreId, sharedMemId)
    {
    }

    LLCMSIProtocol::~LLCMSIProtocol()
    {
    }

    FRFCFS_State LLCMSIProtocol::getRequestState(const Message &msg, FRFCFS_State req_state)
    {
        // GenericCacheLine cache_line;
        // m_data_handler->readLineBits(msg.addr, &cache_line);

        // if (this->m_fsm->isStable(cache_line.state))
        //     return FRFCFS_State::Ready;
        // else if (msg.data != NULL)
        //     return FRFCFS_State::Ready;
        // else if (req_state == FRFCFS_State::Waiting) // replacement request is previously issued
        //     return FRFCFS_State::Waiting;
        // else
        //     return FRFCFS_State::NonReady;

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

    const vector<ControllerAction> &LLCMSIProtocol::processRequest(Message &request_msg)
    {
        GenericCacheLine cache_line;
        EventId event_id;
        int next_state;
        vector<int> actions;

        m_data_handler->readLineBits(request_msg.addr, &cache_line);

        this->readEvent(request_msg, cache_line, &event_id);
        this->m_fsm->getTransition(cache_line.state, (int)event_id, next_state, actions);

        return handleAction(actions, request_msg, cache_line, next_state);
    }

    vector<ControllerAction> &LLCMSIProtocol::handleAction(std::vector<int> &actions, Message &msg,
                                                           GenericCacheLine &cache_line, int next_state)
    {
        this->controller_actions.clear();

        for (int action : actions)
        {
            ControllerAction controller_action;
            switch (static_cast<ActionId>(action))
            {
            case ActionId::Stall:
                std::cout << " LLCMSIProtocol: Stall Transaction is detected" << std::endl;
                exit(0);
                break;

            case ActionId::SendData: // remove request from pending and respond to request
                controller_action.type = ControllerAction::Type::REMOVE_PENDING;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                ((Message *)controller_action.data)->to.clear();
                ((Message *)controller_action.data)->to.push_back(msg.owner); // DualTrans == false
                break;

            case ActionId::GetData:
                // add request to pending requests
                controller_action.type = ControllerAction::Type::ADD_PENDING;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                this->controller_actions.push_back(controller_action);

                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,       // Id
                                                             msg.addr,         // Addr
                                                             0,                // Cycle
                                                             (uint16_t)action, // Complementary_value
                                                             msg.owner);       // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                break;

            case ActionId::SetOwner:
                cache_line.owner_id = msg.owner;
                controller_action.type = ControllerAction::Type::NO_ACTION;
                break;
            case ActionId::ClearOwner:
                cache_line.owner_id = -1;
                controller_action.type = ControllerAction::Type::NO_ACTION;
                break;

            case ActionId::SaveData: // update cacheline (it happens by default if Action is not Stall)
                controller_action.type = ControllerAction::Type::NO_ACTION;
                break;

            case ActionId::IssueInv:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_INV_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,                              // Id
                                                             msg.addr,                                // Addr
                                                             0,                                       // Cycle
                                                             (uint16_t)MSIProtocol::REQUEST_TYPE_INV, // Complementary_value
                                                             (uint16_t)this->m_core_id);              // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_core_id);
                break;

            case ActionId::WriteBack:
                // Do writeback, update cache line
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = (void *)new Message(msg.msg_id, // Id
                                                             msg.addr,   // Addr
                                                             0,          // Cycle
                                                             0,          // Complementary_value
                                                             msg.owner); // Owner
                break;

            case ActionId::Fault:
                std::cout << " LLCMSIProtocol: Fault Transaction is detected" << std::endl;
                exit(0);
                break;
            }

            this->controller_actions.push_back(controller_action);
        }

        // update cache line
        ControllerAction controller_action;

        cache_line.valid = this->m_fsm->isValidState(next_state);
        cache_line.state = next_state;

        controller_action.type = ControllerAction::Type::UPDATE_CACHE_LINE;
        controller_action.data = (void *)new uint8_t[sizeof(Message) + sizeof(cache_line)];

        new (controller_action.data) Message(msg);
        new ((uint8_t *)controller_action.data + sizeof(Message)) GenericCacheLine(cache_line);

        this->controller_actions.push_back(controller_action);

        return this->controller_actions;
    }

    void LLCMSIProtocol::readEvent(Message &msg, GenericCacheLine &cache_line, EventId *out_id)
    {
        switch (msg.source)
        {
        case Message::Source::UPPER_INTERCONNECT:
            if (msg.data != NULL)
                *out_id = EventId::Data_fromUpperInterface;
            break;

        case Message::Source::LOWER_INTERCONNECT:
            if (msg.data != NULL)
                *out_id = EventId::Data_fromLowerInterface;

            else
            {
                switch (msg.complementary_value)
                {
                case MSIProtocol::REQUEST_TYPE_GETS:
                    *out_id = EventId::GetS;
                    break;
                case MSIProtocol::REQUEST_TYPE_GETM:
                    *out_id = EventId::GetM;
                    break;
                case MSIProtocol::REQUEST_TYPE_PUTM:
                    if (msg.owner == m_core_id)
                        *out_id = EventId::Replacement;
                    else
                        *out_id = (msg.owner == cache_line.owner_id) ? EventId::PutM_fromOwner : EventId::PutM_fromNonOwner;
                    break;
                case MSIProtocol::REQUEST_TYPE_INV:
                    if(msg.owner == m_core_id)
                        *out_id = EventId::Own_Invalidation;
                    else
                        std::cout << " LLCMSIProtocol: Invalid Transaction detected on the Bus" << std::endl;
                    break;
                case MSIProtocol::REQUEST_TYPE_SILENT_INV:
                    if(msg.owner == m_core_id)
                        *out_id = EventId::Own_Silent_Inv;
                    else
                        std::cout << " LLCMSIProtocol: Invalid Transaction detected on the Bus" << std::endl;
                    break;
                default: // Invalid Transaction
                    std::cout << " LLCMSIProtocol: Invalid Transaction detected on the Bus" << std::endl;
                    exit(0);
                }
            }
            break;

        default:
            std::cout << "Invalid Message Source at LLC" << std::endl;
        }
    }

    void LLCMSIProtocol::createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line)
    {
        int state = this->m_fsm->getState(string("IorS"));

        m_data_handler->initializeCacheLine(cache_line);
        cache_line->state = state;
        cache_line->valid = true;
    }
}