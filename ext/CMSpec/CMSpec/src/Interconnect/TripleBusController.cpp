/*
 * File  :      TripleBusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2022
 */

#include "../../header/Interconnect/TripleBusController.h"

using namespace std;
namespace ns3
{
    TripleBusController::TripleBusController(vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids) : SplitBusController(interfaces, lower_level_ids)
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
                message_available = true;
                msg = service_buffers[i]->at(0);
                service_buffers[i]->erase(service_buffers[i]->begin());
            }
        }

        if (message_available)
            broadcast(msg, MessageType::SERVICE_REQUEST);
    }
}