/*
 * File  :      MOESIDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#include "../../header/Protocols/MOESIDirectory.h"
using namespace std;

namespace octopus
{
    MOESIDirectory::MOESIDirectory(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MESIDirectory(cache, fsm_path, id, sharedMemId)
    {
    }

    MOESIDirectory::~MOESIDirectory()
    {
    }

    vector<ControllerAction> MOESIDirectory::handleAction(vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        bool putO_found = false;
        bool set_ack_found = false;
        bool data2Req_found = false;
        vector<ControllerAction> controller_actions;
        
        for(auto iter = actions.begin(); iter != actions.end(); )
        {
            if(*iter == (int)ActionId::PutO_Data)
            {
                iter = actions.erase(iter);
                putO_found = true;
                continue;
            }
            else if(*iter == (int)ActionId::SetAck)
            {
                iter = actions.erase(iter);
                set_ack_found = true;
                continue;
            }
            else if(*iter == (int)ActionId::Data2Req)
            {
                iter = actions.erase(iter);
                data2Req_found = true;
                continue;
            }
            iter++;
        }

        controller_actions = MESIDirectory::handleAction(actions, msg, cache_line, next_state);
        
        if(putO_found)
        {
            ControllerAction controller_action;

            // send PutO_Data request
            controller_action.type = ControllerAction::Type::WRITE_BACK;
            controller_action.data = new Message(msg.msg_id,                          // Id
                                                 msg.addr,                            // Addr
                                                 0,                                   // Cycle
                                                 MOESIDirectory::REQUEST_TYPE_PUTO,   // Complementary_value
                                                 (uint16_t)this->m_id);               // Owner
            
            ((Message *)controller_action.data)->to.clear();                
            controller_actions.insert(controller_actions.begin(), controller_action);
        }
        if(set_ack_found)
        {
            ControllerAction controller_action;

            // set Ack count
            controller_action.type = ControllerAction::Type::SET_ACK_NUMBER;
            controller_action.data = new Message(msg);
            
            controller_actions.push_back(controller_action);
        }
        if(data2Req_found)
        {
            ControllerAction controller_action;
            // Do writeback
            controller_action.type = ControllerAction::Type::WRITE_BACK;
            controller_action.data = (void *)new Message(msg);
            
            ((Message *)controller_action.data)->complementary_value = msg.complementary_value2; //copy ack count (if presents) to complementary_value
            ((Message *)controller_action.data)->to.clear();
            
            controller_actions.insert(controller_actions.begin(), controller_action);
        }

        return controller_actions;
    }

    void MOESIDirectory::readEvent(Message &msg, MSIDirectory::EventId *out_id)
    {
        if (msg.source == Message::Source::UPPER_INTERCONNECT && msg.complementary_value2 == MOESIDirectory::RESPONSE_TYPE_ACKCOUNT)
        {
            if(msg.complementary_value == 0)
                *out_id = (MSIDirectory::EventId)EventId::AckCount_zero;
            else
                *out_id = (MSIDirectory::EventId)EventId::AckCount;
            return;
        }
        else if (msg.source == Message::Source::UPPER_INTERCONNECT && msg.data != NULL)
        {
            if(msg.from != m_shared_memory_id && msg.complementary_value != 0) //Check if data is from owner with a non-zero ack (complementary_value) 
            {
                uint64_t addr_key = msg.addr & ~uint64_t(m_data_handler->getBlockSize() -1);
                if((ack_counters->find(addr_key) == ack_counters->end()) ||        //Check if no ack counter is created, then event is DataFromOwner_nonZeroAck
                    ((ack_counters->at(addr_key) + msg.complementary_value) != 0)) //check if all acks are not already received then event is DataFromOwner
                {
                    *out_id = (MSIDirectory::EventId)EventId::DataFromOwner_nonZeroAck;
                    return;
                }
            }
        }
        else if(msg.source == Message::Source::UPPER_INTERCONNECT && msg.complementary_value == MOESIDirectory::REQUEST_TYPE_INVM)
        {
            *out_id = (MSIDirectory::EventId)EventId::InvM;
            return;
        }

        MESIDirectory::readEvent(msg, out_id);
    }
}