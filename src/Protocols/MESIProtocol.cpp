/*
 * File  :      MESIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/MESIProtocol.h"
using namespace std;

namespace octopus
{
    MESIProtocol::MESIProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MSIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    MESIProtocol::~MESIProtocol()
    {
    }

    std::vector<int> MESIProtocol::statesRequireWriteBack()
    {
        vector<int> states = MSIProtocol::statesRequireWriteBack();
        states.push_back(this->m_fsm->getState(string("E")));
        return states;
    }

    void MESIProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        MSIProtocol::readEvent(msg, out_id);

        if (*out_id == MSIProtocol::EventId::OwnData)
            *out_id = (MSIProtocol::EventId)((msg.complementary_value == 2) ? EventId::OwnData_Execlusive : EventId::OwnData);
    }

    vector<ControllerAction> MESIProtocol::handleAction(vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state)
    {
        bool remove_saved_request = false;
        for (int i = 0; i < (int)actions.size(); i++)
        {
            if (actions[i] == (int)ActionId::removeSavedReq)
            {
                actions.erase(actions.begin() + i);
                remove_saved_request = true;
                break;
            }
        }
        std::vector<ControllerAction> controller_actions = MSIProtocol::handleAction(actions, msg, cache_line_info, next_state);

        if (remove_saved_request == true)
        {
            ControllerAction controller_action;

            controller_action.type = (ControllerAction::Type) ((int)ControllerAction::Type::NO_ACTION + 1); //removeSavedRequest //TODO: change it to a constant
            controller_action.data = (void *)new Message;
            ((Message *)controller_action.data)->copy(msg);
            
            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }
}