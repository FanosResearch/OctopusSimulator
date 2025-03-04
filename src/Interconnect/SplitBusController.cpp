/*
 * File  :      SplitBusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#include "../../header/Interconnect/SplitBusController.h"

using namespace std;
namespace octopus
{
    SplitBusController::SplitBusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
            string pname, string config_path, string name) : BusController(map, interfaces, lower_level_ids, pname, config_path, name)
    {
        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);
        if(arbiter_type == STRINGIFY(TDMArbiter))
        {
            m_arbiters.push_back(new TDMArbiter(m_lower_level_ids, m_request_latency));
            m_arbiters.push_back(new TDMArbiter(m_lower_level_ids, m_response_latency));
        }
        else if(arbiter_type == STRINGIFY(FCFSArbiter))
        {
            m_arbiters.push_back(new FCFSArbiter(m_lower_level_ids, m_request_latency));
            m_arbiters.push_back(new FCFSArbiter(m_lower_level_ids, m_response_latency));
        }
        else if(arbiter_type == STRINGIFY(RRArbiter))
        {
            m_arbiters.push_back(new RRArbiter(m_lower_level_ids, m_request_latency));
            m_arbiters.push_back(new RRArbiter(m_lower_level_ids, m_response_latency));
        }
        else
        {
            cout << "Error: there is no matching arbiter" << endl;
            exit(0);
        }

        clk_in_slot_req = 0;
        message_available_req = false;
        BusInterface::getCongregatedBuffers(*m_interfaces, true, &buffers_req);

        clk_in_slot_resp = 0;
        message_available_resp = false;
        BusInterface::getCongregatedBuffers(*m_interfaces, false, &buffers_resp);
    }

    SplitBusController::~SplitBusController()
    {
    }

    void SplitBusController::busStep(uint64_t cycle_number)
    {
        requestBusStep(cycle_number);
        responseBusStep(cycle_number);
    }

    void SplitBusController::requestBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot_req == 0)
            message_available_req = m_arbiters[(int)BusType::RequestBus]->elect(cycle_number, buffers_req, &elected_msg_req);
        else if (clk_in_slot_req == (m_request_latency - 1))
        {
            if (message_available_req)
            {
                broadcast(elected_msg_req);
                message_available_req = false;
            }
        }

        if (message_available_req)
            clk_in_slot_req = (clk_in_slot_req + 1) % m_request_latency;
        else
            clk_in_slot_req = 0;
    }

    void SplitBusController::responseBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot_resp == 0)
            message_available_resp = m_arbiters[(int)BusType::ResponseBus]->elect(cycle_number, buffers_resp, &elected_msg_resp);
        else if (clk_in_slot_resp == (m_response_latency - 1))
        {
            if (message_available_resp)
            {
                send(elected_msg_resp);
                message_available_resp = false;
            }
        }

        if (message_available_resp)
            clk_in_slot_resp = (clk_in_slot_resp + 1) % m_response_latency;
        else
            clk_in_slot_resp = 0;
    }
}