/*
 * File  :      NoCController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#include "../../header/Interconnect/NoCController.h"

using namespace std;
namespace octopus
{
    NoCController::NoCController(ParametersMap pmap, vector<CommunicationInterface *> *interfaces, map<int, vector<int>> *topology, vector<int>& switch_ids,
                                 string pname, string config_path, string name) : MeshController(pmap, interfaces, topology, pname, config_path, name)
    {
        for(int s_id : switch_ids)
        {
            auto itr = std::find(m_cadidates_ids.begin(), m_cadidates_ids.end(), s_id);
            if(itr != m_cadidates_ids.end())
            {
                m_cadidates_ids.erase(itr);
            }
        }

        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);

        for(auto& link : links)
        {
            if(arbiter_type == STRINGIFY(TDMArbiter))
            {
                delete link.arbiter;
                link.arbiter = new TDMArbiter(&m_cadidates_ids, link.latency);
            }
            else if(arbiter_type == STRINGIFY(FCFSArbiter))
                break;
            else if(arbiter_type == STRINGIFY(RRArbiter))
            {
                delete link.arbiter;
                link.arbiter = new RRArbiter(&m_cadidates_ids, link.latency);
            }
            else
            {
                cout << "Error: there is no matching arbiter" << endl;
                exit(0);
            }
        }
    }

    void NoCController::send(Message &msg, int to_id)
    {
        if(msg.data == NULL)
            Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::REQ_BUS_CHECKPOINT);
        else
            Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::RESP_BUS_CHECKPOINT);

        
        auto iter = std::find_if(m_interfaces->begin(), m_interfaces->end(), [=](CommunicationInterface *a)->bool{
            return *a == to_id;
        });
        if(iter == m_interfaces->end()) //not found
        {
            cout << "NoCController: Wrong destination" << endl;
            exit(0);
        }
        
        MessageType type = (msg.data == NULL) ? MessageType::REQUEST : MessageType::DATA_RESPONSE;
        if(!(*iter)->pushMessage2RX(msg, type))
        {
            cout << "NoCController: full buffer" << endl;
            exit(0);
        }
    }

    void NoCController::step(uint64_t cycle_number)
    {
        for(auto& link : links)
        {
            if(link.isReady(cycle_number))
            {
                Message elected_msg;
                if(link.arbiter->elect(cycle_number, link.buffers, &elected_msg))
                {
                    link.utilize(cycle_number, elected_msg);
                }
            }
            else if(link.utilization_cycle == cycle_number)
            {
                if(link.msg.from == link.id_a)
                    send(link.msg, link.id_b);
                else
                    send(link.msg, link.id_a);
            }
        }
    }
}