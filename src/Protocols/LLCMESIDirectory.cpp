/*
 * File  :      LLCMESIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#include "../../header/Protocols/LLCMESIDirectory.h"
using namespace std;

namespace octopus
{
    LLCMESIDirectory::LLCMESIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : LLCMSIDirectory(cache, fsm_path, id, sharedMemId)
    {
    }

    LLCMESIDirectory::~LLCMESIDirectory()
    {
    }

    vector<ControllerAction> LLCMESIDirectory::handleAction(std::vector<int> &actions, Message &msg,
                                                           GenericCacheLine &cache_line, int next_state)
    {
        bool sendEData_found = false;
        bool sendInvM_found = false;
        std::vector<ControllerAction> controller_actions;
        int old_owner = cache_line.owner_id;

        for(int i = 0; i < (int)actions.size(); i++)
        {
            if(actions[i] == (int)ActionId::SendEData)
            {
                actions.erase(actions.begin() + i);
                sendEData_found = true;
                break;
            }
        }
        // (LLCMOESIDirectory strips its own actions 15/16 before calling here, so 15 is SendInvM only in a MESI table)
        for(int i = 0; i < (int)actions.size(); i++)
        {
            if(actions[i] == (int)ActionId::SendInvM)
            {
                actions.erase(actions.begin() + i);
                sendInvM_found = true;
                break;
            }
        }

        controller_actions = LLCMSIDirectory::handleAction(actions, msg, cache_line, next_state);
        

        if(sendEData_found)
        {
            ControllerAction controller_action;

            controller_action.type = (msg.source == Message::Source::LOWER_INTERCONNECT)
                                            ? ControllerAction::Type::HIT_Action
                                            : ControllerAction::Type::REMOVE_PENDING;
            
            controller_action.data = (void *)new Message();
            ((Message *)controller_action.data)->copy(msg);
            ((Message *)controller_action.data)->to.clear();
            ((Message *)controller_action.data)->to.push_back(msg.owner);
            ((Message *)controller_action.data)->complementary_value = MESIDirectory::RESPONSE_TYPE_EXECLUSIVE_DATA;

            controller_actions.push_back(controller_action);
        }

        if(sendInvM_found)
        {
            // the owner's invalidation on an eviction from E or M: it answers once, with the data
            ControllerAction controller_action;
            controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
            controller_action.data = (void *)new Message(msg);
            ((Message *)controller_action.data)->complementary_value = (uint16_t)MESIDirectory::REQUEST_TYPE_INVM;
            ((Message *)controller_action.data)->to.clear();
            ((Message *)controller_action.data)->to.push_back((uint16_t)old_owner);
            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }

    void LLCMESIDirectory::readEvent(Message &msg, GenericCacheLine &cache_line, LLCMSIDirectory::EventId *out_id)
    {
        if(msg.source == Message::Source::LOWER_INTERCONNECT)
        {    
            if(msg.complementary_value == MESIDirectory::REQUEST_TYPE_PUTE)
            {
                if (msg.owner == cache_line.owner_id)
                    *out_id = (LLCMSIDirectory::EventId)EventId::PutE_fromOwner;
                else
                {
                    // a stale PutE from the one sharer left (EI_a + Fwd_GetS): leave S, as for a stale PutM
                    int last = optionalEvent(m_ev_last_pute_nonowner, "last_PutE_fromNonOwner");
                    if (last >= 0 && isLastSharer(msg))
                        *out_id = (LLCMSIDirectory::EventId)last;
                    else
                        *out_id = (LLCMSIDirectory::EventId)EventId::PutE_fromNonOwner;
                }
                return;
            }
        }
        
        LLCMSIDirectory::readEvent(msg, cache_line, out_id);
    }
}