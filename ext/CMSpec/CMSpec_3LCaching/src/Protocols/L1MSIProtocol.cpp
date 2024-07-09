/*
 * File  :      L1MSIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 13, 2024
 */

#include "../../header/Protocols/L1MSIProtocol.h"
using namespace std;

namespace ns3
{
    L1MSIProtocol::L1MSIProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : MSIProtocol(cache, fsm_path, coreId, sharedMemId)
    {
    }

    L1MSIProtocol::~L1MSIProtocol()
    {
    }

    vector<ControllerAction> &L1MSIProtocol::handleAction(std::vector<int> &actions, Message &msg,
                                                        GenericCacheLine &cache_line, int next_state)
    {
        this->controller_actions.clear();

        bool send_data = false;
        bool fault = false;

        for(auto iter = actions.begin(); iter != actions.end(); )
        {
            if(*iter == (int)ActionId::SendData)
            {
                send_data = true;
                iter = actions.erase(iter);
                continue;
            }
            else if(*iter == (int)ActionId::Fault)
            {
                fault = true;
                iter = actions.erase(iter);
                continue;
            }
            iter++;
        }

        MSIProtocol::handleAction(actions, msg, cache_line, next_state);
        
        if(send_data)
        {
            // Do writeback, update cache line
            ControllerAction controller_action;
            controller_action.type = ControllerAction::Type::WRITE_BACK;
            controller_action.data = (void *)new Message(msg);

            ((Message *)controller_action.data)->to.clear();
            
            this->controller_actions.insert(controller_actions.begin(), controller_action);
        }
        else if(fault)
        {
            std::cout << " L1MSIProtocol: Fault Transaction is detected" << std::endl;
            exit(0);
        }        

        return this->controller_actions;
    }

    void L1MSIProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        switch (msg.source)
        {
        case Message::Source::LOWER_INTERCONNECT:
            MSIProtocol::readEvent(msg, out_id);
            return;

        case Message::Source::UPPER_INTERCONNECT:
            if (msg.data != NULL)
            {
                *out_id = (MSIProtocol::EventId)EventId::OwnData;
                return;
            }

            else
            {
                *out_id = (MSIProtocol::EventId)EventId::Invalidation;
                return;
            }

        default:
            std::cout << "Invalid message source" << std::endl;
        }
    }
}