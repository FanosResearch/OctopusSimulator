/*
 * File  :      MESIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 14, 2023
 */

#include "../../header/Protocols/MESIDirectory.h"
using namespace std;

namespace octopus
{
    MESIDirectory::MESIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MSIDirectory(cache, fsm_path, id, sharedMemId)
    {
    }

    MESIDirectory::~MESIDirectory()
    {
    }

    vector<ControllerAction> MESIDirectory::handleAction(vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        bool putE_found = false;
        vector<ControllerAction> controller_actions;
        
        for(int i = 0; i < actions.size(); i++)
        {
            if(actions[i] == (int)ActionId::PutE)
            {
                actions.erase(actions.begin() + i);
                putE_found = true;
                break;
            }
        }

        controller_actions = MSIDirectory::handleAction(actions, msg, cache_line, next_state);
        
        if(putE_found)
        {
            ControllerAction controller_action;

            // send Bus request
            controller_action.type = ControllerAction::Type::SEND_BUS_MSG;
            controller_action.data = (void *)new Message(msg.msg_id,                      // Id
                                                         msg.addr,                        // Addr
                                                         0,                               // Cycle
                                                         MESIDirectory::REQUEST_TYPE_PUTE,// Complementary_value
                                                         (uint16_t)this->m_id);           // Owner
            ((Message *)controller_action.data)->to.push_back((uint16_t)this->m_shared_memory_id);
            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }

    void MESIDirectory::readEvent(Message &msg, MSIDirectory::EventId *out_id)
    {
        if (msg.source == Message::Source::UPPER_INTERCONNECT && msg.data != NULL)
        {
            if(msg.complementary_value == RESPONSE_TYPE_EXECLUSIVE_DATA)
            {    
                *out_id = (MSIDirectory::EventId)EventId::Data_execlusive;
                return;
            }
        }

        MSIDirectory::readEvent(msg, out_id);
    }
}