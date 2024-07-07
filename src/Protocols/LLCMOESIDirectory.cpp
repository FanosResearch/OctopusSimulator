/*
 * File  :      LLCMOESIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#include "../../header/Protocols/LLCMOESIDirectory.h"
using namespace std;

namespace octopus
{
    LLCMOESIDirectory::LLCMOESIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : LLCMESIDirectory(cache, fsm_path, id, sharedMemId)
    {
    }

    LLCMOESIDirectory::~LLCMOESIDirectory()
    {
    }

    vector<ControllerAction> LLCMOESIDirectory::handleAction(std::vector<int> &actions, Message &msg,
                                                           GenericCacheLine &cache_line, int next_state)
    {
        map<ActionId, bool> flags;
        flags[ActionId::sendAckC] = false;
        flags[ActionId::SendInv] = false;
        flags[ActionId::FwdGetM] = false;
        flags[ActionId::SendInvM] = false;
        
        std::vector<ControllerAction> controller_actions;
        int old_owner = cache_line.owner_id;

        for(auto action_iter = actions.begin(); action_iter != actions.end(); )
        {
            bool increment = true;
            for(auto &flag_iter : flags)
            {
                if(*action_iter == (int)flag_iter.first)
                {
                    action_iter = actions.erase(action_iter);
                    flag_iter.second = true;
                    increment = false;
                    break;
                }
            }
            if(increment)
                action_iter++;
        }

        controller_actions = LLCMESIDirectory::handleAction(actions, msg, cache_line, next_state);
        
        if(flags[ActionId::sendAckC])
        {
            ControllerAction controller_action;

            // send Ack count
            controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
            controller_action.data = (void *)new Message(msg.msg_id,             // Id
                                                         msg.addr,               // Addr
                                                         0,                      // Cycle
                                                         0,                      // Complementary_value
                                                         (uint16_t)this->m_id);  // Owner
            ((Message *)controller_action.data)->to.push_back((uint16_t)msg.owner);
            ((Message *)controller_action.data)->complementary_value2 = MOESIDirectory::RESPONSE_TYPE_ACKCOUNT;
            
            uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
            if(protocol_counters->find(addr_key) != protocol_counters->end())
            {
                auto list = protocol_counters->at(addr_key);
                ((Message *)controller_action.data)->complementary_value = list.size(); //add number of sharers to the data response

                if(find(list.begin(), list.end(), msg.owner) != list.end())             //check if the requestor is one of the sharers
                    ((Message *)controller_action.data)->complementary_value--;
            }

            controller_actions.push_back(controller_action);
        }

        if(flags[ActionId::SendInv]) //override sendInv of MSI
        {
            ControllerAction controller_action;

            controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
            controller_action.data = (void *)new Message(msg);

            ((Message *)controller_action.data)->complementary_value = (uint16_t)MSIDirectory::REQUEST_TYPE_INV;
            ((Message *)controller_action.data)->to.clear();

            for(uint64_t id : protocol_counters->at(msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1)))
            {
                if(id == msg.owner) //skip invalidation for the message owner
                    continue;
                if(id == old_owner && msg.owner != m_id) //skip invalidation of a previous owner as it will be invalidated by a fwd message
                    continue;
                ((Message *)controller_action.data)->to.push_back((uint16_t)id);
            }
            
            controller_actions.push_back(controller_action);
        }

        if(flags[ActionId::FwdGetM]) //override fwdGetM of MSI
        {
            ControllerAction controller_action;

            controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
            controller_action.data = (void *)new Message(msg);
            
            ((Message *)controller_action.data)->complementary_value = (uint16_t)MSIDirectory::REQUEST_TYPE_GETM;

            ((Message *)controller_action.data)->to.clear();
            ((Message *)controller_action.data)->to.push_back(old_owner);
            ((Message *)controller_action.data)->complementary_value2 = 0;
            for(uint64_t id : protocol_counters->at(msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1)))
            {
                if(id == msg.owner) //skip invalidation for the message owner
                    continue;
                if(id == old_owner) //skip invalidation of a previous owner as it will be invalidated by a fwd message
                    continue;
                ((Message *)controller_action.data)->complementary_value2++;
            }
            
            controller_actions.push_back(controller_action);
        }

        if(flags[ActionId::SendInvM])
        {
            ControllerAction controller_action;

            controller_action.type = ControllerAction::Type::SEND_FWD_MESSAGE;
            controller_action.data = (void *)new Message(msg);

            ((Message *)controller_action.data)->complementary_value = (uint16_t)MOESIDirectory::REQUEST_TYPE_INVM;
            ((Message *)controller_action.data)->to.clear();
            ((Message *)controller_action.data)->to.push_back((uint16_t)old_owner);
            
            controller_actions.push_back(controller_action);
        }

        return controller_actions;
    }

    void LLCMOESIDirectory::readEvent(Message &msg, GenericCacheLine &cache_line, LLCMSIDirectory::EventId *out_id)
    {
        if(msg.source == Message::Source::LOWER_INTERCONNECT)
        {
            if (msg.data != NULL)
            {
                uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);

                if(msg.complementary_value == MOESIDirectory::REQUEST_TYPE_PUTO)
                {
                    if (msg.owner == cache_line.owner_id)
                    {
                        if((protocol_counters->find(addr_key) != protocol_counters->end()) && ((protocol_counters->at(addr_key).size() - 1) == 0))
                            *out_id = (LLCMSIDirectory::EventId)EventId::last_PutO_fromOwner;
                        else
                            *out_id = (LLCMSIDirectory::EventId)EventId::PutO_Data_fromOwner;
                    }
                    else
                        *out_id = (LLCMSIDirectory::EventId)EventId::PutO_Data_fromNonOwner;
                    return;
                }
                else if(msg.complementary_value == MOESIDirectory::REQUEST_TYPE_INV_ACK)
                {
                    if((protocol_counters->find(addr_key) != protocol_counters->end()) && ((protocol_counters->at(addr_key).size() - 1) == 0))
                    {
                        *out_id = (LLCMSIDirectory::EventId)EventId::Data_fromLowerInterface_lastAck;
                        return;
                    }
                }
            }    
            else if(msg.complementary_value == MSIDirectory::REQUEST_TYPE_GETM &&
                    cache_line.owner_id == msg.owner)
            {
                *out_id = (LLCMSIDirectory::EventId)EventId::GetM_fromOwner;
                return;
            }
        }
        
        LLCMESIDirectory::readEvent(msg, cache_line, out_id);
    }
}