/*
 * File  :      LLCPMESIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 18, 2022
 */

#include "../../header/Protocols/LLCPMESIProtocol.h"
using namespace std;

namespace octopus
{
    LLCPMESIProtocol::LLCPMESIProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : LLCMESIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    LLCPMESIProtocol::~LLCPMESIProtocol()
    {
    }

    vector<ControllerAction> LLCPMESIProtocol::handleAction(vector<int> &actions, Message &msg,
                                                             GenericCacheLine &cache_line_info, int next_state)
    {
        bool wait_data = false;
        bool execlusiveData = false;

        for (int i = 0; i < (int)actions.size(); i++)
        {
            if (actions[i] == (int)ActionId::WaitData)
            {
                actions.erase(actions.begin() + i);
                wait_data = true;
                break;
            }
        }

        for (int i = 0; i < (int)actions.size(); i++)
        {
            if (actions[i] == (int)ActionId::SendExeclusiveData)
            {
                actions.erase(actions.begin() + i);
                execlusiveData = true;
                break;
            }
        }

        std::vector<ControllerAction> controller_actions = LLCMSIProtocol::handleAction(actions, msg, cache_line_info, next_state);

        if (wait_data == true)
        {
            ControllerAction controller_action;
            controller_action.type = ControllerAction::Type::ADD_PENDING;
            controller_action.data = (void *)new Message;
            ((Message *)controller_action.data)->copy(msg);

            controller_actions.push_back(controller_action);
        }

        if (execlusiveData == true)
        {
            ControllerAction controller_action;

            controller_action.type = ControllerAction::Type::REMOVE_PENDING;
            controller_action.data = (void *)new Message;
            ((Message *)controller_action.data)->copy(msg);
            ((Message *)controller_action.data)->complementary_value = 2; //execlusive Data

            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }
}