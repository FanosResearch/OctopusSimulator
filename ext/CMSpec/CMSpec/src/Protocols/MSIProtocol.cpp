/*
 * File  :      MSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/Protocols/MSIProtocol.h"
using namespace std;

namespace ns3
{
    MSIProtocol::MSIProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : CoherenceProtocolHandler(cache, fsm_path, coreId, sharedMemId)
    {
    }

    MSIProtocol::~MSIProtocol()
    {
    }

    FRFCFS_State MSIProtocol::getRequestState(const Message &msg, FRFCFS_State req_state)
    {
        GenericCacheLine cache_line;
        m_data_handler->readLineBits(msg.addr, &cache_line);

        if (this->m_fsm->isStall(cache_line.state, msg.complementary_value))
            return FRFCFS_State::NonReady;

        return FRFCFS_State::Ready;
    }

    std::vector<int> MSIProtocol::statesRequireWriteBack()
    {
        vector<int> states;
        states.push_back(this->m_fsm->getState(string("M")));
        return states;
    }

    const vector<ControllerAction> &MSIProtocol::processRequest(Message &request_msg)
    {
        EventId event_id;
        int next_state;
        vector<int> actions;
        GenericCacheLine cache_line;

        m_data_handler->readLineBits(request_msg.addr, &cache_line);

        this->readEvent(request_msg, &event_id);
        this->m_fsm->getTransition(cache_line.state, (int)event_id, next_state, actions);

        if(event_id == EventId::Other_GetM || event_id == EventId::Other_GetS || event_id == EventId::Invalidation || event_id == EventId::Silent_Inv)
        {
            if(actions.size() != 0 || next_state != cache_line.state)
                Counters::inc(Counters::num_interference, m_core_id);
        }

        return handleAction(actions, request_msg, cache_line, next_state);
    }

    vector<ControllerAction> &MSIProtocol::handleAction(std::vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        this->controller_actions.clear();


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

            this->controller_actions.push_back(controller_action);
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
                // std::cout << " MSIProtocol: Stall Transaction is detected" << std::endl;
                // exit(0);
                break;

            case ActionId::Hit: // remove request from pending and respond to cpu, update cache line
                controller_action.type = (msg.source == Message::Source::LOWER_INTERCONNECT)
                                             ? ControllerAction::Type::HIT_Action
                                             : ControllerAction::Type::REMOVE_PENDING;
                controller_action.data = (void *)new Message(msg);
                this->controller_actions.push_back(controller_action);

                if(controller_action.type == ControllerAction::Type::HIT_Action)
                    Counters::inc(Counters::num_hit, m_core_id);

                controller_action.type = ControllerAction::Type::MODIFY_DATA;
                controller_action.data = (void *)new Message(msg);
                break;

            case ActionId::GetS:
            case ActionId::GetM:
                // add request to pending requests
                controller_action.type = ControllerAction::Type::ADD_PENDING;
                controller_action.data = (void *)new Message();
                ((Message *)controller_action.data)->copy(msg);
                this->controller_actions.push_back(controller_action);

                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id, // Id
                                                             msg.addr,   // Addr
                                                             0,          // Cycle
                                                             (action == (int)ActionId::GetS) ? MSIProtocol::REQUEST_TYPE_GETS
                                                                                             : MSIProtocol::REQUEST_TYPE_GETM, // Complementary_value
                                                             (uint16_t)this->m_core_id);                                       // Owner

                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                
                Counters::inc(Counters::num_miss, m_core_id);
                break;
            case ActionId::PutM:
                // send Bus request, update cache line
                controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
                controller_action.data = (void *)new Message(msg.msg_id,                     // Id
                                                             msg.addr,                       // Addr
                                                             0,                              // Cycle
                                                             MSIProtocol::REQUEST_TYPE_PUTM, // Complementary_value
                                                             (uint16_t)this->m_core_id);     // Owner
                ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                break;

            case ActionId::Data2Req:
            case ActionId::Data2Both:
                // Do writeback, update cache line
                controller_action.type = ControllerAction::Type::WRITE_BACK;
                controller_action.data = (void *)new Message(msg);

                ((Message *)controller_action.data)->to.clear();
                if (action == (int)ActionId::Data2Both)
                    ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
                
                this->controller_actions.insert(controller_actions.begin(), controller_action);
                continue;
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
                std::cout << " MSIProtocol: Fault Transaction is detected" << std::endl;
                exit(0);
                break;
            }

            this->controller_actions.push_back(controller_action);
        }

        return this->controller_actions;
    }

    void MSIProtocol::readEvent(Message &msg, EventId *out_id)
    {
        switch (msg.source)
        {
        case Message::Source::LOWER_INTERCONNECT:
            // *out_id = (msg.complementary_value == CpuFIFO::REQTYPE::READ) ? EventId::Load : (msg.complementary_value == CpuFIFO::REQTYPE::WRITE) ? EventId::Store
            //                                                                                                                                      : EventId::Replacement;
            *out_id = (msg.complementary_value == 0) ? EventId::Load : (msg.complementary_value == 1) ? EventId::Store : EventId::Replacement;
            return;

        case Message::Source::UPPER_INTERCONNECT:
            if (msg.data != NULL)
                *out_id = EventId::OwnData;

            else
            {
                switch (msg.complementary_value)
                {
                case MSIProtocol::REQUEST_TYPE_GETS:
                    *out_id = (msg.owner == m_core_id) ? EventId::Own_GetS : EventId::Other_GetS;
                    return;
                case MSIProtocol::REQUEST_TYPE_GETM:
                    *out_id = (msg.owner == m_core_id) ? EventId::Own_GetM : EventId::Other_GetM;
                    return;
                case MSIProtocol::REQUEST_TYPE_PUTM:
                    *out_id = (msg.owner == m_core_id) ? EventId::Own_PutM : EventId::Other_PutM;
                    return;
                case MSIProtocol::REQUEST_TYPE_INV:
                    *out_id = EventId::Invalidation;
                    return;
                case MSIProtocol::REQUEST_TYPE_SILENT_INV:
                    *out_id = EventId::Silent_Inv;
                    return;
                default: // Invalid Transaction
                    std::cout << " MSIProtocol: Invalid Transaction detected on the Bus" << std::endl;
                    exit(0);
                }
            }
            return;

        default:
            std::cout << "Invalid message source" << std::endl;
        }
    }
}