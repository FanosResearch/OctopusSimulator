/*
 * File  :      L2MSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 13, 2024
 */

#include "../../header/Protocols/L2MSIProtocol.h"
using namespace std;

namespace ns3
{
    L2MSIProtocol::L2MSIProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : MSIProtocol(cache, fsm_path, coreId, sharedMemId)
    {
    }

    L2MSIProtocol::~L2MSIProtocol()
    {
    }

    vector<ControllerAction> &L2MSIProtocol::handleAction(std::vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        this->controller_actions.clear();

        bool send_inv = false;
        bool save_data = false;

        for(auto iter = actions.begin(); iter != actions.end(); )
        {
            if(*iter == (int)ActionId::SendInv)
            {
                send_inv = true;
                iter = actions.erase(iter);
                continue;
            }
            else if(*iter == (int)ActionId::SaveData)
            {
                save_data = true;
                iter = actions.erase(iter);
                continue;
            }
            iter++;
        }

        MSIProtocol::handleAction(actions, msg, cache_line, next_state);
        
        if(send_inv)
        {
            ControllerAction controller_action;
            controller_action.type = ControllerAction::Type::SEND_INV_MSG;
            controller_action.data = (void *)new Message(msg.msg_id,                              // Id
                                                         msg.addr,                                // Addr
                                                         0,                                       // Cycle
                                                         (uint16_t)MSIProtocol::REQUEST_TYPE_INV, // Complementary_value
                                                         (uint16_t)this->m_core_id);              // Owner
            ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_core_id);
            
            this->controller_actions.push_back(controller_action);
        }
        else if(save_data && controller_actions.empty())
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

        return this->controller_actions;
    }

    void L2MSIProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        if(msg.source == Message::Source::LOWER_INTERCONNECT && msg.data != NULL)
        {
            *out_id = (MSIProtocol::EventId)EventId::DataFromLowerInterface;
            return;
        }

        MSIProtocol::readEvent(msg, out_id);
    }
}