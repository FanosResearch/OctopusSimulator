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

            // Match stable's SendData model: a request arriving on the lower interface
            // (an L1 GetS the LLC can satisfy directly) is served inline via HIT_Action
            // and never enters m_pending_requests; only a fill coming back (source !=
            // LOWER) drains a parked request via REMOVE_PENDING.
            controller_action.type = (msg.source == Message::Source::LOWER_INTERCONNECT)
                                         ? ControllerAction::Type::HIT_Action
                                         : ControllerAction::Type::REMOVE_PENDING;
            controller_action.data = (void *)new Message;
            ((Message *)controller_action.data)->copy(msg);
            // Retarget to the L1 requestor. copy(msg) inherits msg.to, which for data
            // triggered by a memory/write-back message is the memory id -- sending that
            // onto the L1 bus trips "Wrong destination". (Mirrors the MSI SendData path.)
            ((Message *)controller_action.data)->to.clear();
            ((Message *)controller_action.data)->to.push_back(msg.owner);
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