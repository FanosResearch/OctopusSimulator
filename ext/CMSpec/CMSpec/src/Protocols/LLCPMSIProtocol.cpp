/*
 * File  :      LLCPMSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 25, 2021
 */

#include "../../header/Protocols/LLCPMSIProtocol.h"
using namespace std;

namespace ns3
{
    LLCPMSIProtocol::LLCPMSIProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : LLCMSIProtocol(cache, fsm_path, coreId, sharedMemId)
    {
    }

    LLCPMSIProtocol::~LLCPMSIProtocol()
    {
    }
    
    vector<ControllerAction> &LLCPMSIProtocol::handleAction(vector<int> &actions, Message &msg,
                                                           GenericCacheLine &cache_line_info, int next_state)
    {
        bool wait_data = false;
        for (int i = 0; i < (int)actions.size(); i++)
        {
            if (actions[i] == (int)ActionId::WaitData)
            {
                actions.erase(actions.begin() + i);
                wait_data = true;
                break;
            }
        }
        LLCMSIProtocol::handleAction(actions, msg, cache_line_info, next_state);

        if (wait_data == true)
        {
            ControllerAction controller_action;
            controller_action.type = ControllerAction::Type::ADD_PENDING;
            controller_action.data = (void *)new Message;
            ((Message*)controller_action.data)->copy(msg);

            this->controller_actions.push_back(controller_action);
        }
        return this->controller_actions;
    }
}