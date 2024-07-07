/*
 * File  :      MeshController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#include "../../header/Interconnect/MeshController.h"

using namespace std;
namespace octopus
{
    MeshController::MeshController(ParametersMap pmap, vector<CommunicationInterface *> *interfaces, map<int, vector<int>> *topology,
                                   string pname, string config_path, string name) : Configurable(pmap, config_path, name, pname)
    {
        m_interfaces = interfaces;

        //Parameters initialization
        m_request_latency = std::get<int>(parameters.at(STRINGIFY(m_request_latency)).value);
        m_data_latency = std::get<int>(parameters.at(STRINGIFY(m_data_latency)).value);

        //Constructor
        for(auto interface : *interfaces)
            m_cadidates_ids.push_back(interface->m_interface_id);
        initializeLinks(topology);
        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name, parent_name + "." + name);
    }

    MeshController::~MeshController()
    {
        for (Link link : links)
        {
            delete link.arbiter;
        }
        delete dprint;
    }

    void MeshController::initializeLinks(map<int, vector<int>> *topology)
    {
        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);

        for(auto iter_map : *topology)
        {
            for(auto iter_vec : iter_map.second)
            {
                Link l1(iter_map.first, iter_vec, LinkType::RequestLink, m_request_latency);
                Link l2(iter_map.first, iter_vec, LinkType::DataLink, m_data_latency);

                if(std::find(links.begin(), links.end(), l1) == links.end())
                    links.push_back(l1); //not found
                if(std::find(links.begin(), links.end(), l2) == links.end())
                    links.push_back(l2); //not found
            }
        }

        for(auto& link : links)
        {
            if(arbiter_type == STRINGIFY(TDMArbiter))
                link.arbiter = new TDMArbiter(&m_cadidates_ids, link.latency);
            else if(arbiter_type == STRINGIFY(FCFSArbiter))
                link.arbiter = new FCFSArbiter(&link.ids, link.latency);
            else if(arbiter_type == STRINGIFY(RRArbiter))
                link.arbiter = new RRArbiter(&m_cadidates_ids, link.latency);
            else
            {
                cout << "Error: there is no matching arbiter" << endl;
                exit(0);
            }

            link.setInterfaceBuffers(m_interfaces);
        }
    }

    void MeshController::send(Message &msg)
    {
        if(msg.data == NULL)
            Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::REQ_BUS_CHECKPOINT);
        else
            Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::RESP_BUS_CHECKPOINT);

        for (int i = 0; i < (int)msg.to.size(); i++)
        {
            auto iter = std::find_if(m_interfaces->begin(), m_interfaces->end(), [=](CommunicationInterface *a)->bool{
                return *a == msg.to[i];
            });
            if(iter == m_interfaces->end()) //not found
            {
                cout << "MeshController: Wrong destination" << endl;
                exit(0);
            }
            
            MessageType type = (msg.data == NULL) ? MessageType::REQUEST : MessageType::DATA_RESPONSE;
            if(!(*iter)->pushMessage2RX(msg, type))
            {
                cout << "MeshController: full buffer" << endl;
                exit(0);
            }
        }
    }

    void MeshController::step(uint64_t cycle_number)
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
                send(link.msg);
        }
    }
}