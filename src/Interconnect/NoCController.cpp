/*
 * File  :      NoCController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#include "../../header/Interconnect/NoCController.h"

using namespace std;
namespace ns3
{
    NoCController::NoCController(ParametersMap pmap, vector<CommunicationInterface *> *interfaces, map<int, vector<int>> *topology, vector<int>& switch_ids,
                                 string pname, string config_path, string name) : MeshController(pmap, interfaces, topology, pname, config_path, name)
    {
        auto old_candidate = m_candidates_ids;
        for(int s_id : switch_ids)
        {
            auto itr = std::find(m_candidates_ids.begin(), m_candidates_ids.end(), s_id);
            if(itr != m_candidates_ids.end())
            {
                m_candidates_ids.erase(itr);
            }
        }

        if(old_candidate.size() != m_candidates_ids.size())
        {
            string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);

            for(auto& link : links)
            {
                if(arbiter_type == STRINGIFY(TDMArbiter))
                {
                    delete link.arbiter;
                    link.arbiter = new TDMArbiter(&m_candidates_ids, link.latency);
                }
                else if(arbiter_type == STRINGIFY(FCFSArbiter))
                    break;
                else if(arbiter_type == STRINGIFY(RRArbiter))
                {
                    delete link.arbiter;
                    link.arbiter = new RRArbiter(&m_candidates_ids, link.latency);
                }
                else if(arbiter_type == STRINGIFY(RROFArbiter))
                    break;
                else
                {
                    cout << "Error: there is no matching arbiter" << endl;
                    exit(0);
                }
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
                // Was using msg.from to decide direction, but in multi-hop
                // routes msg.from is the PROTOCOL-level original sender and
                // doesn't match either link endpoint. msg.prev_hop is updated
                // on every hop (NoCInterface::noc_pushMessage) and reliably
                // matches one of the link's endpoints.
                if(link.msg.prev_hop == link.id_a)
                    send(link.msg, link.id_b);
                else
                    send(link.msg, link.id_a);
            }
        }
    }
}