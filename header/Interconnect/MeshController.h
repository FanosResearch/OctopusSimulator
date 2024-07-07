/*
 * File  :      MeshController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#ifndef _MESH_CONTROLLER_H
#define _MESH_CONTROLLER_H

#include "MeshInterface.h"
#include "Configurable.h"
#include "DebugPrint.h"
#include "CommunicationInterface.h"

#include "Arbiter.h"
#include "TDMArbiter.h"
#include "FCFSArbiter.h"
#include "RRArbiter.h"

#include "Logger.h"

#include <vector>
#include <map>
#include <iostream>

using namespace std;

namespace octopus
{
    class MeshController : public Configurable
    {
    public:
        enum class LinkType
        {
            RequestLink = 0,
            DataLink,
        };

        class Link
        {
        public:
            const int id_a; //interface id of one side
            const int id_b; //interface id of the other side
            const LinkType type;
            const int latency;
        
            uint64_t utilization_cycle;
            
            Arbiter* arbiter = NULL;
            vector<int> ids;
            vector<vector<Message> *> buffers;
            Message msg;

            Link(int a, int b, LinkType type, int latency) 
                :id_a(a), id_b(b), type(type), latency(latency) 
            {
                utilization_cycle = 0;
                ids.push_back(a);
                ids.push_back(b);
            }
            ~Link()
            {
                if(arbiter)
                    delete arbiter;
            }
            bool isReady(uint64_t cycle_number){ return (utilization_cycle < cycle_number); }
            void utilize(uint64_t cycle_number, Message& msg){ utilization_cycle = cycle_number + latency - 1; this->msg = msg;}
            void setInterfaceBuffers(vector<CommunicationInterface *> *interfaces)
            {
                vector<CommunicationInterface *> sub_interfaces;
                for(auto interface : *interfaces)
                {
                    if(*interface == id_a || *interface == id_b)
                        sub_interfaces.push_back(interface);
                }
                MeshInterface::getCongregatedBuffers(sub_interfaces, type == LinkType::RequestLink, &buffers);
            }
            bool operator == (const Link& link) const
            {
                return (this->id_a == link.id_a && this->id_b == link.id_b && this->type == link.type) ||
                       (this->id_a == link.id_b && this->id_b == link.id_a && this->type == link.type);
            }
        };

    protected:
        vector<CommunicationInterface *> *m_interfaces;
        vector<Link> links;

        DebugPrint* dprint;

        vector<int> m_cadidates_ids;
        int m_request_latency;
        int m_data_latency;

        void initializeLinks(map<int, vector<int>> *topology);
        virtual void send(Message &msg);

    public:
        MeshController(ParametersMap pmap, vector<CommunicationInterface *> *interfaces, map<int, vector<int>> *topology,
                      string pname = "",
                      string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                      string name = STRINGIFY(MeshController));
        ~MeshController();

        virtual void step(uint64_t cycle_number);
    };
}

#endif /* _MESH_CONTROLLER_H */
