/*
 * File  :      SplitBusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#include "../../header/Interconnect/SplitBusController.h"
#include "../../header/Logger.h"

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

        // In-flight reserve for the response back-pressure high-water mark. Optional
        // config param; default 0 (stall exactly at full). Tune to >= the worst-case
        // number of in-flight (already-broadcast) responses so an emit never overflows.
        if (parameters.find(STRINGIFY(response_reserve)) != parameters.end())
            m_response_reserve = std::get<int>(parameters.at(STRINGIFY(response_reserve)).value);

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

    bool SplitBusController::responseBackpressured()
    {
        // Back-pressure is asserted while ANY agent's response buffer free space has
        // dropped into its in-flight reserve: a new coherence transaction could make
        // some responder emit a response that overflows before it drains. Stalling the
        // request bus until space frees is the flow-control a real split-transaction
        // bus uses. The reserve holds responses from already-broadcast (in-flight)
        // transactions so their emits never overflow -- controllers are never gated.
        for (CommunicationInterface *itf : *m_interfaces)
            if (itf->txResponseFreeSlots() <= m_response_reserve)
                return true;
        return false;
    }

    void SplitBusController::requestBusStep(uint64_t cycle_number)
    {
        // Bus-level flow control: while a response buffer is within its reserve,
        // serialize NO new coherence request (do not elect, broadcast, or advance the
        // slot). The response bus keeps draining (responseBusStep runs regardless), so
        // this is deadlock-free; controllers are never frozen mid-transaction, so
        // coherence order and per-transaction atomicity hold.
        if (responseBackpressured())
            return;

        // Slot timing. A message is elected in slot 0 and delivered in slot
        // latency-1, so a latency of 1 elects and delivers in the same cycle.
        // The arbiter removes the message from its sender at election, which is
        // why the delivery must never be skipped: an if/else-if form skips it
        // at latency 1 (slot 0 always takes the election branch) and drops
        // every message. Election is also guarded on no message being held, so
        // a held broadcast is retried rather than overwritten.
        if (clk_in_slot_req == 0 && !message_available_req)
        {
            message_available_req = m_arbiters[(int)BusType::RequestBus]->elect(cycle_number, buffers_req, &elected_msg_req);
            if (message_available_req)   // trace: grant = start of the slot
                Logger::getLogger()->trace(elected_msg_req, m_is_mem_bus ? Logger::Role::MEM_BUS : Logger::Role::REQ_BUS, m_is_mem_bus ? 1u : 0u, Logger::Phase::ENTER);
        }

        if (message_available_req && clk_in_slot_req == (m_request_latency - 1))
        {
            // Atomic broadcast: consume the elected message only once it can be
            // delivered to ALL receivers. If any receiver's RX buffer is full,
            // hold it (stay in this slot) and retry next cycle -- never
            // partially deliver or drop.
            if (broadcast(elected_msg_req))
            {
                message_available_req = false;
                clk_in_slot_req = 0;
            }
            return;
        }

        if (message_available_req)
            clk_in_slot_req++;
        else
            clk_in_slot_req = 0;
    }

    void SplitBusController::responseBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot_resp == 0 && !message_available_resp)
        {
            message_available_resp = m_arbiters[(int)BusType::ResponseBus]->elect(cycle_number, buffers_resp, &elected_msg_resp);
            if (message_available_resp)
                Logger::getLogger()->trace(elected_msg_resp, m_is_mem_bus ? Logger::Role::MEM_BUS : Logger::Role::RESP_BUS, m_is_mem_bus ? 1u : 0u, Logger::Phase::ENTER);
        }

        if (message_available_resp && clk_in_slot_resp == (m_response_latency - 1))
        {
            send(elected_msg_resp);
            message_available_resp = false;
            clk_in_slot_resp = 0;
            return;
        }

        if (message_available_resp)
            clk_in_slot_resp++;
        else
            clk_in_slot_resp = 0;
    }
}