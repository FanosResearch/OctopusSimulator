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

        // The base maps the self-injected bank-read-complete message to its own
        // DataArrayReady id (MSI column layout). MESI inserts OwnData_Execlusive
        // ahead of DataArrayReady, so the base's DataArrayReady id collides
        // numerically with MESI's OwnData_Execlusive: remap to MESI's id and
        // return before the OwnData_Execlusive test below.
        if (*out_id == MSIProtocol::EventId::DataArrayReady)
        {
            *out_id = (MSIProtocol::EventId)EventId::DataArrayReady;
            return;
        }

        if (*out_id == MSIProtocol::EventId::OwnData)
            *out_id = (MSIProtocol::EventId)((msg.complementary_value == 2) ? EventId::OwnData_Execlusive : EventId::OwnData);

        // Exclusive data was signalled via complementary_value==2; clear it now so a
        // downstream re-read of this message does not re-trigger OwnData_Execlusive.
        if ((int)*out_id == (int)EventId::OwnData_Execlusive)
            msg.complementary_value = 0;
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
        // MESI's action layout inserts removeSavedReq(9) ahead of StartRead(10),
        // but the base's switch keys StartRead on its own id (9). Retarget so the
        // base runs its identical timed-bank-read (START_READ) construction.
        // removeSavedReq and StartRead never co-occur in a transition, and the
        // removeSavedReq strip above already ran, so there is no id-9 collision.
        for (int i = 0; i < (int)actions.size(); i++)
            if (actions[i] == (int)ActionId::StartRead)
                actions[i] = (int)MSIProtocol::ActionId::StartRead;
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