/*
 * File  :      PMSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 11, 2021
 */

#include "../../header/Protocols/PMSIProtocol.h"
using namespace std;

namespace octopus
{
    PMSIProtocol::PMSIProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MSIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    PMSIProtocol::~PMSIProtocol()
    {
    }

    vector<ControllerAction> PMSIProtocol::handleAction(vector<int> &actions, Message &msg,
                                                         GenericCacheLine &cache_line_info, int next_state)
    {
        bool putm_non_demanding = false;
        for (int i = 0; i < (int)actions.size(); i++)
        {
            if (actions[i] == (int)ActionId::PutM_nonDem)
            {
                actions.erase(actions.begin() + i);
                putm_non_demanding = true;
                break;
            }
        }
        std::vector<ControllerAction> controller_actions = MSIProtocol::handleAction(actions, msg, cache_line_info, next_state);

        if (putm_non_demanding == true)
        {
            ControllerAction controller_action;
            // send Bus request
            controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
            controller_action.data = (void *)new Message(msg.msg_id,                        // Id
                                                         msg.addr,                          // Addr
                                                         0,                                 // Cycle
                                                         (uint16_t)ActionId::PutM_nonDem,   // Complementary_value
                                                         (uint16_t)this->m_id);        // Owner
            ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);

            controller_actions.push_back(controller_action);
        }
        return controller_actions;
    }
}