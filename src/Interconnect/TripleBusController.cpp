/*
 * File  :      TripleBusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2022
 */

#include "../../header/Interconnect/TripleBusController.h"

using namespace std;
namespace octopus
{
    TripleBusController::TripleBusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
        string pname, string config_path, string name) : SplitBusController(map, interfaces, lower_level_ids, pname, config_path, name)
    {
    }

    TripleBusController::~TripleBusController()
    {
    }

    void TripleBusController::busStep(uint64_t cycle_number)
    {
        SplitBusController::busStep(cycle_number);
        serviceBusStep(cycle_number);
    }

    void TripleBusController::serviceBusStep(uint64_t cycle_number)
    {
        vector<vector<Message>*> service_buffers;
        bool message_available = false;
        Message msg;

        TripleBusInterface::getCongregatedServiceBuffers(*m_interfaces, &service_buffers);
        for(int i = 0; i < (int)service_buffers.size(); i++)
        {
            if(!service_buffers[i]->empty())
            {
                // Atomic service broadcast: dequeue this back-invalidation only if
                // it can be delivered to ALL receivers this cycle; otherwise leave
                // it queued and retry next cycle. One service message per cycle.
                msg = service_buffers[i]->at(0);
                if (broadcast(msg, MessageType::SERVICE_REQUEST))
                    service_buffers[i]->erase(service_buffers[i]->begin());
                message_available = true;
                break;
            }
        }
        (void)message_available;
    }
}