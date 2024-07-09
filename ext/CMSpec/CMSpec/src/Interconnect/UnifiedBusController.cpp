/*
 * File  :      UnifiedBusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 20, 2022
 */

#include "../../header/Interconnect/UnifiedBusController.h"

using namespace std;
namespace ns3
{
    UnifiedBusController::UnifiedBusController(vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids)
        : BusController(interfaces, lower_level_ids)
    {
        // m_arbiters.push_back(new TDMArbiter(m_lower_level_ids, m_request_latency + m_response_latency));
        m_arbiters.push_back(new RRArbiter(m_lower_level_ids, m_request_latency + m_response_latency));

        clk_in_slot = 0;
        message_available = false;
    }

    UnifiedBusController::~UnifiedBusController()
    {
    }

    void UnifiedBusController::busStep(uint64_t cycle_number)
    {
        requestBusStep(cycle_number);
    }

    void UnifiedBusController::requestBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot == 0)
        {
            vector<vector<Message> *> buffers;

            BusInterface::getCongregatedBuffers(*m_interfaces, true, &buffers);
            message_available = m_arbiters[(int)BusType::RequestBus]->elect(cycle_number, buffers, &elected_msg);
        }
        else if (clk_in_slot == (m_request_latency - 1))
        {
            if (message_available)
                broadcast(elected_msg);
        }
        else if (clk_in_slot == m_request_latency)
        {
            vector<vector<Message> *> buffers;

            BusInterface::getCongregatedBuffers(*m_interfaces, false, &buffers);
            message_available = m_arbiters[(int)BusType::RequestBus]->forceElect(cycle_number, buffers, &elected_msg);
        }
        else if (clk_in_slot == (m_request_latency + m_response_latency - 1))
        {
            if (message_available)
            {
                send(elected_msg);
                message_available = false;
            }
        }

        if (message_available)
            clk_in_slot = (clk_in_slot + 1) % (m_request_latency + m_response_latency);
        else
            clk_in_slot = 0;
    }
}