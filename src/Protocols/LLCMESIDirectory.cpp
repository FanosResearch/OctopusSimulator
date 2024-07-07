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
        std::vector<ControllerAction> controller_actions;

        for(int i = 0; i < actions.size(); i++)
        {
            if(actions[i] == (int)ActionId::SendEData)
            {
                actions.erase(actions.begin() + i);
                sendEData_found = true;
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
                    *out_id = (LLCMSIDirectory::EventId)EventId::PutE_fromNonOwner;
                return;
            }
        }
        
        LLCMSIDirectory::readEvent(msg, cache_line, out_id);
    }
}