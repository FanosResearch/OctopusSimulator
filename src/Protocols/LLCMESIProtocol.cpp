/*
 * File  :      LLCMESIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/LLCMESIProtocol.h"
using namespace std;

namespace octopus
{
    LLCMESIProtocol::LLCMESIProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : LLCMSIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    LLCMESIProtocol::~LLCMESIProtocol()
    {
    }

    vector<ControllerAction> LLCMESIProtocol::handleAction(vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state)
    {
        bool execlusiveData = false;
        
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

    void LLCMESIProtocol::createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line)
    {
        int state = this->m_fsm->getState(string("I"));

        m_data_handler->initializeCacheLine(cache_line);
        cache_line->state = state;
        cache_line->valid = true;
    }
}