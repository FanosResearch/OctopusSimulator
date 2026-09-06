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
        // Candidate list = every agent that transmits on this bus (all interface ids:
        // the lower-level L1s AND the upper-level LLC). An L1-only list starves
        // LLC-sourced traffic (owner = LLC id) under owner-matched arbiters (RR/TDM),
        // which hang; FCFS ignores owner so it happened to work. Use the full set.
        for (CommunicationInterface *itf : *m_interfaces)
            m_arbiter_candidate_ids.push_back(itf->m_interface_id);
        vector<int> *cands = &m_arbiter_candidate_ids;

        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);
        if(arbiter_type == STRINGIFY(TDMArbiter))
        {
            m_arbiters.push_back(new TDMArbiter(cands, m_request_latency));
            m_arbiters.push_back(new TDMArbiter(cands, m_response_latency));
        }
        else if(arbiter_type == STRINGIFY(FCFSArbiter))
        {
            m_arbiters.push_back(new FCFSArbiter(cands, m_request_latency));
            m_arbiters.push_back(new FCFSArbiter(cands, m_response_latency));
        }
        else if(arbiter_type == STRINGIFY(RRArbiter))
        {
            m_arbiters.push_back(new RRArbiter(cands, m_request_latency));
            m_arbiters.push_back(new RRArbiter(cands, m_response_latency));
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
                // Atomic broadcast: consume the elected message only once it can be
                // delivered to ALL receivers. If any receiver's RX buffer is full,
                // hold it and retry next cycle -- never partially deliver or drop.
                if (broadcast(elected_msg_req))
                    message_available_req = false;
            }
        }

        if (message_available_req)
        {
            // Advance toward the broadcast slot; once there, stay put to retry a
            // failed broadcast rather than wrapping to 0 (which would re-elect and
            // drop the still-undelivered message).
            if (clk_in_slot_req < (m_request_latency - 1))
                clk_in_slot_req++;
        }
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